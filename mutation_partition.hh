/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 */

/*
 * This file is part of Scylla.
 *
 * Scylla is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Scylla is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Scylla.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <iostream>
#include <map>
#include <boost/intrusive/set.hpp>
#include <boost/range/iterator_range.hpp>
#include <boost/range/adaptor/indexed.hpp>
#include <boost/range/adaptor/filtered.hpp>

#include "schema.hh"
#include "keys.hh"
#include "atomic_cell.hh"
#include "query-result-writer.hh"
#include "mutation_partition_view.hh"
#include "utils/managed_vector.hh"

//
// Container for cells of a row. Cells are identified by column_id.
//
// All cells must belong to a single column_kind. The kind is not stored
// for space-efficiency reasons. Whenever a method accepts a column_kind,
// the caller must always supply the same column_kind.
//
// Can be used as a range of row::cell_entry.
//
class row {
    class cell_entry {
        boost::intrusive::set_member_hook<> _link;
        column_id _id;
        atomic_cell_or_collection _cell;
        friend class row;
    public:
        cell_entry(column_id id, atomic_cell_or_collection cell)
            : _id(id)
            , _cell(std::move(cell))
        { }
        cell_entry(cell_entry&&) noexcept;
        cell_entry(const cell_entry&) noexcept;

        column_id id() const { return _id; }
        const atomic_cell_or_collection& cell() const { return _cell; }
        atomic_cell_or_collection& cell() { return _cell; }

        struct compare {
            bool operator()(const cell_entry& e1, const cell_entry& e2) const {
                return e1._id < e2._id;
            }
            bool operator()(column_id id1, const cell_entry& e2) const {
                return id1 < e2._id;
            }
            bool operator()(const cell_entry& e1, column_id id2) const {
                return e1._id < id2;
            }
        };
    };

    using size_type = std::make_unsigned_t<column_id>;

    enum class storage_type {
        vector,
        set,
    };
    storage_type _type = storage_type::vector;
    size_type _size = 0;

    using map_type = boost::intrusive::set<cell_entry,
        boost::intrusive::member_hook<cell_entry, boost::intrusive::set_member_hook<>, &cell_entry::_link>,
        boost::intrusive::compare<cell_entry::compare>, boost::intrusive::constant_time_size<false>>;
public:
    static constexpr size_t max_vector_size = 32;
    static constexpr size_t internal_count = (sizeof(map_type) + sizeof(cell_entry)) / sizeof(atomic_cell_or_collection);
private:
    using vector_type = managed_vector<atomic_cell_or_collection, internal_count, size_type>;

    union storage {
        storage() { }
        ~storage() { }
        map_type set;
        vector_type vector;
    } _storage;
public:
    row();
    ~row();
    row(const row&);
    row(row&& other);
    row& operator=(row&& other);
    size_t size() const { return _size; }

    void reserve(column_id);

    const atomic_cell_or_collection& cell_at(column_id id) const;

    // Returns a pointer to cell's value or nullptr if column is not set.
    const atomic_cell_or_collection* find_cell(column_id id) const;
private:
    template<typename Func>
    void remove_if(Func&& func) {
        if (_type == storage_type::vector) {
            for (unsigned i = 0; i < _storage.vector.size(); i++) {
                auto& c = _storage.vector[i];
                if (!bool(c)) {
                    continue;
                }
                if (func(i, c)) {
                    c = atomic_cell_or_collection();
                    _size--;
                }
            }
        } else {
            for (auto it = _storage.set.begin(); it != _storage.set.end();) {
                if (func(it->id(), it->cell())) {
                    auto& entry = *it;
                    it = _storage.set.erase(it);
                    current_allocator().destroy(&entry);
                    _size--;
                } else {
                    ++it;
                }
            }
        }
    }

private:
    auto get_range_vector() const {
        auto range = boost::make_iterator_range(_storage.vector.begin(), _storage.vector.end());
        return range | boost::adaptors::filtered([] (const atomic_cell_or_collection& c) { return bool(c); })
               | boost::adaptors::transformed([this] (const atomic_cell_or_collection& c) {
            auto id = &c - _storage.vector.data();
            return std::pair<column_id, const atomic_cell_or_collection&>(id, std::cref(c));
        });
    }
    auto get_range_set() const {
        auto range = boost::make_iterator_range(_storage.set.begin(), _storage.set.end());
        return range | boost::adaptors::transformed([] (const cell_entry& c) {
            return std::pair<column_id, const atomic_cell_or_collection&>(c.id(), c.cell());
        });
    }
    template<typename Func>
    auto with_both_ranges(const row& other, Func&& func) const;

    void vector_to_set();
public:
    template<typename Func>
    void for_each_cell(Func&& func) const {
        for_each_cell_until([func = std::forward<Func>(func)] (column_id id, const atomic_cell_or_collection& c) {
            func(id, c);
            return stop_iteration::no;
        });
    }

    template<typename Func>
    void for_each_cell_until(Func&& func) const {
        if (_type == storage_type::vector) {
            for (unsigned i = 0; i < _storage.vector.size(); i++) {
                auto& cell = _storage.vector[i];
                if (!bool(cell)) {
                    continue;
                }
                if (func(i, cell) == stop_iteration::yes) {
                    break;
                }
            }
        } else {
            for (auto& cell : _storage.set) {
                const auto& c = cell.cell();
                if (c && func(cell.id(), c) == stop_iteration::yes) {
                    break;
                }
            }
        }
    }

    template<typename Func>
    void for_each_cell_until(Func&& func) {
        if (_type == storage_type::vector) {
            for (unsigned i = 0; i < _storage.vector.size(); i++) {
                auto& cell = _storage.vector[i];
                if (!bool(cell)) {
                    continue;
                }
                if (func(i, cell) == stop_iteration::yes) {
                    break;
                }
            }
        } else {
            for (auto& cell : _storage.set) {
                auto& c = cell.cell();
                if (c && func(cell.id(), c) == stop_iteration::yes) {
                    break;
                }
            }
        }
    }

    // Merges cell's value into the row.
    void apply(const column_definition& column, const atomic_cell_or_collection& cell);

    //
    // Merges cell's value into the row.
    //
    // In case of exception the current object and external object (moved-from)
    // are both left in some valid states, such that they still will commute to
    // a state the current object would have should the exception had not occurred.
    //
    void apply(const column_definition& column, atomic_cell_or_collection&& cell);

    // Adds cell to the row. The column must not be already set.
    void append_cell(column_id id, atomic_cell_or_collection cell);

    void merge(const schema& s, column_kind kind, const row& other);

    // In case of exception the current object and external object (moved-from)
    // are both left in some valid states, such that they still will commute to
    // a state the current object would have should the exception had not occurred.
    void merge(const schema& s, column_kind kind, row&& other);

    // Expires cells based on query_time. Expires tombstones based on gc_before
    // and max_purgeable. Removes cells covered by tomb.
    // Returns true iff there are any live cells left.
    bool compact_and_expire(const schema& s, column_kind kind, tombstone tomb, gc_clock::time_point query_time,
        api::timestamp_type max_purgeable, gc_clock::time_point gc_before);

    row difference(const schema&, column_kind, const row& other) const;

    bool operator==(const row&) const;

    friend std::ostream& operator<<(std::ostream& os, const row& r);
};

std::ostream& operator<<(std::ostream& os, const std::pair<column_id, const atomic_cell_or_collection&>& c);

class row_marker;
int compare_row_marker_for_merge(const row_marker& left, const row_marker& right);

class row_marker {
    static constexpr gc_clock::duration no_ttl { 0 };
    static constexpr gc_clock::duration dead { -1 };
    api::timestamp_type _timestamp = api::missing_timestamp;
    gc_clock::duration _ttl = no_ttl;
    gc_clock::time_point _expiry;
public:
    row_marker() = default;
    row_marker(api::timestamp_type created_at) : _timestamp(created_at) { }
    row_marker(api::timestamp_type created_at, gc_clock::duration ttl, gc_clock::time_point expiry)
        : _timestamp(created_at), _ttl(ttl), _expiry(expiry)
    { }
    row_marker(tombstone deleted_at)
        : _timestamp(deleted_at.timestamp), _ttl(dead), _expiry(deleted_at.deletion_time)
    { }
    bool is_missing() const {
        return _timestamp == api::missing_timestamp;
    }
    bool is_live() const {
        return !is_missing() && _ttl != dead;
    }
    bool is_live(tombstone t, gc_clock::time_point now) const {
        if (is_missing() || _ttl == dead) {
            return false;
        }
        if (_ttl != no_ttl && _expiry < now) {
            return false;
        }
        return _timestamp > t.timestamp;
    }
    // Can be called only when !is_missing().
    bool is_dead(gc_clock::time_point now) const {
        if (_ttl == dead) {
            return true;
        }
        return _ttl != no_ttl && _expiry < now;
    }
    // Can be called only when is_live().
    bool is_expiring() const {
        return _ttl != no_ttl;
    }
    // Can be called only when is_expiring().
    gc_clock::duration ttl() const {
        return _ttl;
    }
    // Can be called only when is_expiring().
    gc_clock::time_point expiry() const {
        return _expiry;
    }
    // Can be called only when is_dead().
    gc_clock::time_point deletion_time() const {
        return _ttl == dead ? _expiry : _expiry - _ttl;
    }
    api::timestamp_type timestamp() const {
        return _timestamp;
    }
    void apply(const row_marker& rm) {
        if (compare_row_marker_for_merge(*this, rm) < 0) {
            *this = rm;
        }
    }
    // Expires cells and tombstones. Removes items covered by higher level
    // tombstones.
    // Returns true if row marker is live.
    bool compact_and_expire(tombstone tomb, gc_clock::time_point now,
            api::timestamp_type max_purgeable, gc_clock::time_point gc_before) {
        if (is_missing()) {
            return false;
        }
        if (_timestamp <= tomb.timestamp) {
            _timestamp = api::missing_timestamp;
            return false;
        }
        if (_ttl > no_ttl && _expiry < now) {
            _expiry -= _ttl;
            _ttl = dead;
        }
        if (_ttl == dead && _timestamp < max_purgeable && _expiry < gc_before) {
            _timestamp = api::missing_timestamp;
        }
        return !is_missing() && _ttl != dead;
    }
    bool operator==(const row_marker& other) const {
        if (_timestamp != other._timestamp) {
            return false;
        }
        if (is_missing()) {
            return true;
        }
        if (_ttl != other._ttl) {
            return false;
        }
        return _ttl == no_ttl || _expiry == other._expiry;
    }
    bool operator!=(const row_marker& other) const {
        return !(*this == other);
    }
    friend std::ostream& operator<<(std::ostream& os, const row_marker& rm);
};

class deletable_row final {
    tombstone _deleted_at;
    row_marker _marker;
    row _cells;
public:
    deletable_row() {}

    void apply(tombstone deleted_at) {
        _deleted_at.apply(deleted_at);
    }

    void apply(const row_marker& rm) {
        _marker.apply(rm);
    }
public:
    tombstone deleted_at() const { return _deleted_at; }
    api::timestamp_type created_at() const { return _marker.timestamp(); }
    row_marker& marker() { return _marker; }
    const row_marker& marker() const { return _marker; }
    const row& cells() const { return _cells; }
    row& cells() { return _cells; }
    friend std::ostream& operator<<(std::ostream& os, const deletable_row& dr);
    bool equal(const schema& s, const deletable_row& other) const;
    bool is_live(const schema& s, tombstone base_tombstone, gc_clock::time_point query_time) const;
    bool empty() const { return !_deleted_at && _marker.is_missing() && !_cells.size(); }
    deletable_row difference(const schema&, column_kind, const deletable_row& other) const;
};

class row_tombstones_entry {
    boost::intrusive::set_member_hook<> _link;
    clustering_key_prefix _prefix;
    tombstone _t;
    friend class mutation_partition;
public:
    row_tombstones_entry(clustering_key_prefix&& prefix, tombstone t)
        : _prefix(std::move(prefix))
        , _t(std::move(t))
    { }
    row_tombstones_entry(row_tombstones_entry&& o) noexcept;
    row_tombstones_entry(const row_tombstones_entry&) = default;
    clustering_key_prefix& prefix() {
        return _prefix;
    }
    const clustering_key_prefix& prefix() const {
        return _prefix;
    }
    tombstone& t() {
        return _t;
    }
    const tombstone& t() const {
        return _t;
    }
    void apply(tombstone t) {
        _t.apply(t);
    }
    struct compare {
        clustering_key_prefix::less_compare _c;
        compare(const schema& s) : _c(s) {}
        bool operator()(const row_tombstones_entry& e1, const row_tombstones_entry& e2) const {
            return _c(e1._prefix, e2._prefix);
        }
        bool operator()(const clustering_key_prefix& prefix, const row_tombstones_entry& e) const {
            return _c(prefix, e._prefix);
        }
        bool operator()(const row_tombstones_entry& e, const clustering_key_prefix& prefix) const {
            return _c(e._prefix, prefix);
        }
    };
    template <typename Comparator>
    struct delegating_compare {
        Comparator _c;
        delegating_compare(Comparator&& c) : _c(std::move(c)) {}
        template <typename Comparable>
        bool operator()(const Comparable& prefix, const row_tombstones_entry& e) const {
            return _c(prefix, e._prefix);
        }
        template <typename Comparable>
        bool operator()(const row_tombstones_entry& e, const Comparable& prefix) const {
            return _c(e._prefix, prefix);
        }
    };
    template <typename Comparator>
    static auto key_comparator(Comparator&& c) {
        return delegating_compare<Comparator>(std::move(c));
    }

    friend std::ostream& operator<<(std::ostream& os, const row_tombstones_entry& rte);
    bool equal(const schema& s, const row_tombstones_entry& other) const;
};

class rows_entry {
    boost::intrusive::set_member_hook<> _link;
    clustering_key _key;
    deletable_row _row;
    friend class mutation_partition;
public:
    rows_entry(clustering_key&& key)
        : _key(std::move(key))
    { }
    rows_entry(const clustering_key& key)
        : _key(key)
    { }
    rows_entry(const clustering_key& key, deletable_row&& row)
        : _key(key), _row(std::move(row))
    { }
    rows_entry(const clustering_key& key, const deletable_row& row)
        : _key(key), _row(row)
    { }
    rows_entry(rows_entry&& o) noexcept;
    rows_entry(const rows_entry& e)
        : _key(e._key)
        , _row(e._row)
    { }
    clustering_key& key() {
        return _key;
    }
    const clustering_key& key() const {
        return _key;
    }
    deletable_row& row() {
        return _row;
    }
    const deletable_row& row() const {
        return _row;
    }
    void apply(tombstone t) {
        _row.apply(t);
    }
    struct compare {
        clustering_key::less_compare _c;
        compare(const schema& s) : _c(s) {}
        bool operator()(const rows_entry& e1, const rows_entry& e2) const {
            return _c(e1._key, e2._key);
        }
        bool operator()(const clustering_key& key, const rows_entry& e) const {
            return _c(key, e._key);
        }
        bool operator()(const rows_entry& e, const clustering_key& key) const {
            return _c(e._key, key);
        }
        bool operator()(const clustering_key_view& key, const rows_entry& e) const {
            return _c(key, e._key);
        }
        bool operator()(const rows_entry& e, const clustering_key_view& key) const {
            return _c(e._key, key);
        }
    };
    template <typename Comparator>
    struct delegating_compare {
        Comparator _c;
        delegating_compare(Comparator&& c) : _c(std::move(c)) {}
        template <typename Comparable>
        bool operator()(const Comparable& v, const rows_entry& e) const {
            return _c(v, e._key);
        }
        template <typename Comparable>
        bool operator()(const rows_entry& e, const Comparable& v) const {
            return _c(e._key, v);
        }
    };
    template <typename Comparator>
    static auto key_comparator(Comparator&& c) {
        return delegating_compare<Comparator>(std::move(c));
    }
    friend std::ostream& operator<<(std::ostream& os, const rows_entry& re);
    bool equal(const schema& s, const rows_entry& other) const;
};

namespace db {
template<typename T>
class serializer;
}


class mutation_partition final {
    // FIXME: using boost::intrusive because gcc's std::set<> does not support heterogeneous lookup yet
    using rows_type = boost::intrusive::set<rows_entry,
        boost::intrusive::member_hook<rows_entry, boost::intrusive::set_member_hook<>, &rows_entry::_link>,
        boost::intrusive::compare<rows_entry::compare>>;
    using row_tombstones_type = boost::intrusive::set<row_tombstones_entry,
        boost::intrusive::member_hook<row_tombstones_entry, boost::intrusive::set_member_hook<>, &row_tombstones_entry::_link>,
        boost::intrusive::compare<row_tombstones_entry::compare>>;
    friend rows_entry;
    friend row_tombstones_entry;
    friend class size_calculator;
private:
    tombstone _tombstone;
    row _static_row;
    rows_type _rows;
    // Contains only strict prefixes so that we don't have to lookup full keys
    // in both _row_tombstones and _rows.
    // FIXME: using boost::intrusive because gcc's std::set<> does not support heterogeneous lookup yet
    row_tombstones_type _row_tombstones;

    template<typename T>
    friend class db::serializer;
    friend class mutation_partition_applier;
public:
    mutation_partition(schema_ptr s)
        : _rows(rows_entry::compare(*s))
        , _row_tombstones(row_tombstones_entry::compare(*s))
    { }
    mutation_partition(mutation_partition&&) = default;
    mutation_partition(const mutation_partition&);
    ~mutation_partition();
    mutation_partition& operator=(const mutation_partition& x);
    mutation_partition& operator=(mutation_partition&& x) = default;
    bool equal(const schema& s, const mutation_partition&) const;
    friend std::ostream& operator<<(std::ostream& os, const mutation_partition& mp);
public:
    void apply(tombstone t) { _tombstone.apply(t); }
    void apply_delete(const schema& schema, const exploded_clustering_prefix& prefix, tombstone t);
    void apply_delete(const schema& schema, clustering_key&& key, tombstone t);
    void apply_delete(const schema& schema, clustering_key_view key, tombstone t);
    // Equivalent to applying a mutation with an empty row, created with given timestamp
    void apply_insert(const schema& s, clustering_key_view, api::timestamp_type created_at);
    // prefix must not be full
    void apply_row_tombstone(const schema& schema, clustering_key_prefix prefix, tombstone t);
    void apply_row_tombstone(const schema&, row_tombstones_entry*) noexcept;
    //
    // Applies p to current object.
    //
    // Basic exception guarantees. If apply() throws after being called in
    // some entry state p0, the object is left in some consistent state p1 and
    // it's possible that p1 != p0 + p. It holds though that p1 + p = p0 + p.
    //
    // FIXME: make stronger exception guarantees (p1 = p0).
    //
    void apply(const schema& schema, const mutation_partition& p);
    //
    // Same guarantees as for apply(const schema&, const mutation_partition&).
    //
    // In case of exception the current object and external object (moved-from)
    // are both left in some valid states, such that they still will commute to
    // a state the current object would have should the exception had not occurred.
    //
    void apply(const schema& schema, mutation_partition&& p);
    // Same guarantees as for apply(const schema&, const mutation_partition&).
    void apply(const schema& schema, mutation_partition_view);
private:
    void insert_row(const schema& s, const clustering_key& key, deletable_row&& row);
    void insert_row(const schema& s, const clustering_key& key, const deletable_row& row);

    uint32_t do_compact(const schema& s,
        gc_clock::time_point now,
        const std::vector<query::clustering_range>& row_ranges,
        bool reverse,
        uint32_t row_limit,
        api::timestamp_type max_purgeable);

    // Calls func for each row entry inside row_ranges until func returns stop_iteration::yes.
    // Removes all entries for which func didn't return stop_iteration::no or wasn't called at all.
    // If reversed is true, func will be called on entries in reverse order. In that case row_ranges
    // must be already in reverse order.
    template<bool reversed, typename Func>
    void trim_rows(const schema& s,
        const std::vector<query::clustering_range>& row_ranges,
        Func&& func);
public:
    // Performs the following:
    //   - throws out data which doesn't belong to row_ranges
    //   - expires cells and tombstones based on query_time
    //   - drops cells covered by higher-level tombstones (compaction)
    //   - leaves at most row_limit live rows
    //
    // Note: a partition with a static row which has any cell live but no
    // clustered rows still counts as one row, according to the CQL row
    // counting rules.
    //
    // Returns the count of CQL rows which remained. If the returned number is
    // smaller than the row_limit it means that there was no more data
    // satisfying the query left.
    //
    // The row_limit parameter must be > 0.
    //
    uint32_t compact_for_query(const schema& s, gc_clock::time_point query_time,
        const std::vector<query::clustering_range>& row_ranges, bool reversed, uint32_t row_limit);

    // Performs the following:
    //   - expires cells based on compaction_time
    //   - drops cells covered by higher-level tombstones
    //   - drops expired tombstones which timestamp is before max_purgeable
    void compact_for_compaction(const schema& s, api::timestamp_type max_purgeable,
        gc_clock::time_point compaction_time);

    // Returns the minimal mutation_partition that when applied to "other" will
    // create a mutation_partition equal to the sum of other and this one.
    mutation_partition difference(schema_ptr s, const mutation_partition& other) const;

    // Returns true if there is no live data or tombstones.
    bool empty() const;
public:
    deletable_row& clustered_row(const clustering_key& key);
    deletable_row& clustered_row(clustering_key&& key);
    deletable_row& clustered_row(const schema& s, const clustering_key_view& key);
public:
    tombstone partition_tombstone() const { return _tombstone; }
    row& static_row() { return _static_row; }
    const row& static_row() const { return _static_row; }
    // return a set of rows_entry where each entry represents a CQL row sharing the same clustering key.
    const rows_type& clustered_rows() const { return _rows; }
    const row_tombstones_type& row_tombstones() const { return _row_tombstones; }
    const row* find_row(const clustering_key& key) const;
    const rows_entry* find_entry(const schema& schema, const clustering_key_prefix& key) const;
    tombstone range_tombstone_for_row(const schema& schema, const clustering_key& key) const;
    tombstone tombstone_for_row(const schema& schema, const clustering_key& key) const;
    tombstone tombstone_for_row(const schema& schema, const rows_entry& e) const;
    boost::iterator_range<rows_type::const_iterator> range(const schema& schema, const query::range<clustering_key_prefix>& r) const;
    boost::iterator_range<rows_type::iterator> range(const schema& schema, const query::range<clustering_key_prefix>& r);
    // Returns at most "limit" rows. The limit must be greater than 0.
    void query(query::result::partition_writer& pw, const schema& s, gc_clock::time_point now, uint32_t limit = query::max_rows) const;

    // Returns the number of live CQL rows in this partition.
    //
    // Note: If no regular rows are live, but there's something live in the
    // static row, the static row counts as one row. If there is at least one
    // regular row live, static row doesn't count.
    //
    size_t live_row_count(const schema&,
        gc_clock::time_point query_time = gc_clock::time_point::min()) const;

    bool is_static_row_live(const schema&,
        gc_clock::time_point query_time = gc_clock::time_point::min()) const;
private:
    template<typename Func>
    void for_each_row(const schema& schema, const query::range<clustering_key_prefix>& row_range, bool reversed, Func&& func) const;
};

/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Copyright 2015 Cloudius Systems
 *
 * Modified by Cloudius Systems
 */

#pragma once

#include "cql3/restrictions/abstract_restriction.hh"
#include "cql3/term.hh"
#include "core/shared_ptr.hh"
#include "to_string.hh"
#include "exceptions/exceptions.hh"

namespace cql3 {

namespace restrictions {

class term_slice final {
private:
    struct bound {
        bool inclusive;
        ::shared_ptr<term> t;
    };
    bound _bounds[2];
private:
    term_slice(::shared_ptr<term> start, bool include_start, ::shared_ptr<term> end, bool include_end)
        : _bounds{{include_start, std::move(start)}, {include_end, std::move(end)}}
    { }
public:
    static term_slice new_instance(statements::bound bound, bool include, ::shared_ptr<term> term) {
        if (is_start(bound)) {
            return term_slice(std::move(term), include, {}, false);
        } else {
            return term_slice({}, false, std::move(term), include);
        }
    }

    /**
     * Returns the boundary value.
     *
     * @param bound the boundary type
     * @return the boundary value
     */
    ::shared_ptr<term> bound(statements::bound b) const {
        return _bounds[get_idx(b)].t;
    }

    /**
     * Checks if this slice has a boundary for the specified type.
     *
     * @param b the boundary type
     * @return <code>true</code> if this slice has a boundary for the specified type, <code>false</code> otherwise.
     */
    bool has_bound(statements::bound b) const {
        return bool(_bounds[get_idx(b)].t);
    }

    /**
     * Checks if this slice boundary is inclusive for the specified type.
     *
     * @param b the boundary type
     * @return <code>true</code> if this slice boundary is inclusive for the specified type,
     * <code>false</code> otherwise.
     */
    bool is_inclusive(statements::bound b) const {
        return !_bounds[get_idx(b)].t || _bounds[get_idx(b)].inclusive;
    }

    /**
     * Merges this slice with the specified one.
     *
     * @param other the slice to merge with
     */
    void merge(const term_slice& other) {
        if (has_bound(statements::bound::START)) {
            assert(!other.has_bound(statements::bound::START));
            _bounds[get_idx(statements::bound::END)] = other._bounds[get_idx(statements::bound::END)];
        } else {
            assert(!other.has_bound(statements::bound::END));
            _bounds[get_idx(statements::bound::START)] = other._bounds[get_idx(statements::bound::START)];
        }
    }

    friend std::ostream& operator<<(std::ostream& out, const term_slice& slice) {
        static auto print_term = [] (::shared_ptr<term> t) -> sstring {
            return t ? t->to_string() : "null";
        };
        return out << sprint("(%s %s, %s %s)",
            slice._bounds[0].inclusive ? ">=" : ">", print_term(slice._bounds[0].t),
            slice._bounds[1].inclusive ? "<=" : "<", print_term(slice._bounds[1].t));
    }

#if 0
    /**
     * Returns the index operator corresponding to the specified boundary.
     *
     * @param b the boundary type
     * @return the index operator corresponding to the specified boundary
     */
    public Operator getIndexOperator(statements::bound b)
    {
        if (b.isStart())
            return boundInclusive[get_idx(b)] ? Operator.GTE : Operator.GT;

        return boundInclusive[get_idx(b)] ? Operator.LTE : Operator.LT;
    }

    /**
     * Check if this <code>TermSlice</code> is supported by the specified index.
     *
     * @param index the Secondary index
     * @return <code>true</code> this type of <code>TermSlice</code> is supported by the specified index,
     * <code>false</code> otherwise.
     */
    public bool isSupportedBy(SecondaryIndex index)
    {
        bool supported = false;

        if (has_bound(statements::bound::START))
            supported |= isInclusive(statements::bound::START) ? index.supportsOperator(Operator.GTE)
                    : index.supportsOperator(Operator.GT);
        if (has_bound(statements::bound::END))
            supported |= isInclusive(statements::bound::END) ? index.supportsOperator(Operator.LTE)
                    : index.supportsOperator(Operator.LT);

        return supported;
    }
#endif
};

}
}
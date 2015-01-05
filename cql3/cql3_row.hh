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
 * Modified by Cloudius Systems
 * Copyright 2015 Cloudius Systems
 */

#pragma once

namespace cql3 {
// FIXME: stub
class cql3_row {
public:
    class builder {
    };
    class row_iterator {
    };
};
}

#if 0
package org.apache.cassandra.cql3;

import java.nio.ByteBuffer;
import java.util.Iterator;
import java.util.List;

import org.apache.cassandra.db.Cell;

public interface CQL3Row
{
    public ByteBuffer getClusteringColumn(int i);
    public Cell getColumn(ColumnIdentifier name);
    public List<Cell> getMultiCellColumn(ColumnIdentifier name);

    public interface Builder
    {
        public RowIterator group(Iterator<Cell> cells);
    }

    public interface RowIterator extends Iterator<CQL3Row>
    {
        public CQL3Row getStaticRow();
    }
}
#endif
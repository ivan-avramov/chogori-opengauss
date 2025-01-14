/* -------------------------------------------------------------------------
 *
 * pg_range.cpp
 *	  routines to support manipulation of the pg_range relation
 *
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/common/backend/catalog/pg_range.cpp
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"
#include "knl/knl_variable.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_opclass.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_range.h"
#include "catalog/pg_type.h"
#include "utils/fmgroids.h"
#include "utils/rel.h"
#include "utils/rel_gs.h"
#include "utils/snapmgr.h"

/*
 * RangeCreate
 *		Create an entry in pg_range.
 */
void RangeCreate(Oid rangeTypeOid, Oid rangeSubType, Oid rangeCollation, Oid rangeSubOpclass,
    RegProcedure rangeCanonical, RegProcedure rangeSubDiff)
{
    Relation pg_range;
    Datum values[Natts_pg_range];
    bool nulls[Natts_pg_range];
    HeapTuple tup;
    ObjectAddress myself;
    ObjectAddress referenced;

    pg_range = heap_open(RangeRelationId, RowExclusiveLock);

    errno_t rc = memset_s(nulls, sizeof(nulls), 0, sizeof(nulls));
    securec_check(rc, "", "");

    values[Anum_pg_range_rngtypid - 1] = ObjectIdGetDatum(rangeTypeOid);
    values[Anum_pg_range_rngsubtype - 1] = ObjectIdGetDatum(rangeSubType);
    values[Anum_pg_range_rngcollation - 1] = ObjectIdGetDatum(rangeCollation);
    values[Anum_pg_range_rngsubopc - 1] = ObjectIdGetDatum(rangeSubOpclass);
    values[Anum_pg_range_rngcanonical - 1] = ObjectIdGetDatum(rangeCanonical);
    values[Anum_pg_range_rngsubdiff - 1] = ObjectIdGetDatum(rangeSubDiff);

    tup = heap_form_tuple(RelationGetDescr(pg_range), values, nulls);

    (void)simple_heap_insert(pg_range, tup);
    CatalogUpdateIndexes(pg_range, tup);
    heap_freetuple_ext(tup);

    /* record type's dependencies on range-related items */

    myself.classId = TypeRelationId;
    myself.objectId = rangeTypeOid;
    myself.objectSubId = 0;

    referenced.classId = TypeRelationId;
    referenced.objectId = rangeSubType;
    referenced.objectSubId = 0;
    recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);

    referenced.classId = OperatorClassRelationId;
    referenced.objectId = rangeSubOpclass;
    referenced.objectSubId = 0;
    recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);

    if (OidIsValid(rangeCollation)) {
        referenced.classId = CollationRelationId;
        referenced.objectId = rangeCollation;
        referenced.objectSubId = 0;
        recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);
    }

    if (OidIsValid(rangeCanonical)) {
        referenced.classId = ProcedureRelationId;
        referenced.objectId = rangeCanonical;
        referenced.objectSubId = 0;
        recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);
    }

    if (OidIsValid(rangeSubDiff)) {
        referenced.classId = ProcedureRelationId;
        referenced.objectId = rangeSubDiff;
        referenced.objectSubId = 0;
        recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);
    }

    heap_close(pg_range, RowExclusiveLock);
}

/*
 * RangeDelete
 *		Remove the pg_range entry for the specified type.
 */
void RangeDelete(Oid rangeTypeOid)
{
    Relation pg_range;
    ScanKeyData key[1];
    SysScanDesc scan;
    HeapTuple tup;

    pg_range = heap_open(RangeRelationId, RowExclusiveLock);

    ScanKeyInit(&key[0], Anum_pg_range_rngtypid, BTEqualStrategyNumber, F_OIDEQ, ObjectIdGetDatum(rangeTypeOid));

    scan = systable_beginscan(pg_range, RangeTypidIndexId, true, NULL, 1, key);

    while (HeapTupleIsValid(tup = systable_getnext(scan))) {
        CatalogTupleDelete(pg_range, tup);
    }

    systable_endscan(scan);

    heap_close(pg_range, RowExclusiveLock);
}

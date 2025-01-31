/* -------------------------------------------------------------------------
 *
 * indexing.cpp
 *	  This file contains routines to support indexes defined on system
 *	  catalogs.
 *
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/common/backend/catalog/indexing.cpp
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"
#include "knl/knl_variable.h"

#include "access/k2/k2pg_aux.h"
#include "catalog/index.h"
#include "catalog/indexing.h"
#include "executor/executor.h"
#include "utils/rel.h"
#include "utils/rel_gs.h"
#include "utils/syscache.h"

#include "access/k2/k2pg_aux.h"
#include "access/k2/k2_table_ops.h"

/*
 * CatalogOpenIndexes - open the indexes on a system catalog.
 *
 * When inserting or updating tuples in a system catalog, call this
 * to prepare to update the indexes for the catalog.
 *
 * In the current implementation, we share code for opening/closing the
 * indexes with execUtils.c.  But we do not use ExecInsertIndexTuples,
 * because we don't want to create an EState.  This implies that we
 * do not support partial or expressional indexes on system catalogs,
 * nor can we support generalized exclusion constraints.
 * This could be fixed with localized changes here if we wanted to pay
 * the extra overhead of building an EState.
 */
CatalogIndexState CatalogOpenIndexes(Relation heapRel)
{
    ResultRelInfo* resultRelInfo = NULL;

    resultRelInfo = makeNode(ResultRelInfo);
    resultRelInfo->ri_RangeTableIndex = 1; /* dummy */
    resultRelInfo->ri_RelationDesc = heapRel;
    resultRelInfo->ri_TrigDesc = NULL; /* we don't fire triggers */

    ExecOpenIndices(resultRelInfo, false);

    return resultRelInfo;
}

/*
 * CatalogCloseIndexes - clean up resources allocated by CatalogOpenIndexes
 */
void CatalogCloseIndexes(CatalogIndexState indstate)
{
    ExecCloseIndices(indstate);
    pfree(indstate);
    indstate = NULL;
}

// TODO: should we introduce CatalogIndexDelete() and use it in CatalogUpdateIndexes()?

/*
 * CatalogIndexInsert - insert index entries for one catalog tuple
 *
 * This should be called for each inserted or updated catalog tuple.
 *
 * This is effectively a cut-down version of ExecInsertIndexTuples.
 */
void CatalogIndexInsert(CatalogIndexState indstate, HeapTuple heapTuple)
{
    int i;
    int numIndexes;
    RelationPtr relationDescs;
    Relation heapRelation;
    TupleTableSlot* slot = NULL;
    IndexInfo** indexInfoArray;
    Datum values[INDEX_MAX_KEYS];
    bool isnull[INDEX_MAX_KEYS];

    /* HOT update does not require index inserts */
    if (HeapTupleIsHeapOnly(heapTuple))
        return;

    /*
     * Get information from the state structure.  Fall out if nothing to do.
     */
    numIndexes = indstate->ri_NumIndices;
    if (numIndexes == 0)
        return;
    relationDescs = indstate->ri_IndexRelationDescs;
    indexInfoArray = indstate->ri_IndexRelationInfo;
    heapRelation = indstate->ri_RelationDesc;

    /* Need a slot to hold the tuple being examined */
    slot = MakeSingleTupleTableSlot(RelationGetDescr(heapRelation));
    (void)ExecStoreTuple(heapTuple, slot, InvalidBuffer, false);

    /*
     * for each index, form and insert the index tuple
     */
    for (i = 0; i < numIndexes; i++) {
		/*
		 * No need to update K2PG primary key which is intrinsic part of
		 * the base table.
		 */
		if (IsK2PgEnabled() && relationDescs[i]->rd_index->indisprimary)
			continue;

        IndexInfo* indexInfo = NULL;

        indexInfo = indexInfoArray[i];

        /* If the index is marked as read-only, ignore it */
        if (!indexInfo->ii_ReadyForInserts)
            continue;

        /*
         * Expressional and partial indexes on system catalogs are not
         * supported, nor exclusion constraints, nor deferred uniqueness
         */
        Assert(indexInfo->ii_Expressions == NIL);
        Assert(indexInfo->ii_Predicate == NIL);
        Assert(indexInfo->ii_ExclusionOps == NULL);
        Assert(relationDescs[i]->rd_index->indimmediate);
        Assert(indexInfo->ii_NumIndexKeyAttrs != 0);

        /*
         * FormIndexDatum fills in its values and isnull parameters with the
         * appropriate values for the column(s) of the index.
         */
        FormIndexDatum(indexInfo,
            slot,
            NULL, /* no expression eval to do */
            values,
            isnull);

        /*
         * The index AM does the rest.
         */
        ItemPointer t_self = IsK2PgRelation(relationDescs[i]) ? (ItemPointer)(heapTuple->t_k2pgctid) : &(heapTuple->t_self);
        (void)index_insert(relationDescs[i], /* index relation */
            values,                    /* array of index Datums */
            isnull,                    /* is-null flags */
            t_self,      /* tid of heap tuple */
            heapRelation,
            relationDescs[i]->rd_index->indisunique ? UNIQUE_CHECK_YES : UNIQUE_CHECK_NO);
    }

    ExecDropSingleTupleTableSlot(slot);
}

/*
 * CatalogIndexDelete - delete index entries for one catalog tuple
 *
 * This should be called for each updated or deleted catalog tuple.
 *
 * This is effectively a cut-down version of ExecDeleteIndexTuples.
 */
static void
CatalogIndexDelete(CatalogIndexState indstate, HeapTuple heapTuple)
{
	int			i;
	int			numIndexes;
	RelationPtr relationDescs;
	Relation	heapRelation;
	TupleTableSlot *slot;
	IndexInfo **indexInfoArray;
	Datum		values[INDEX_MAX_KEYS];
	bool		isnull[INDEX_MAX_KEYS];

	/*
	 * Get information from the state structure.  Fall out if nothing to do.
	 */
	numIndexes = indstate->ri_NumIndices;
	if (numIndexes == 0)
		return;
	relationDescs = indstate->ri_IndexRelationDescs;
	indexInfoArray = indstate->ri_IndexRelationInfo;
	heapRelation = indstate->ri_RelationDesc;

	/* Need a slot to hold the tuple being examined */
	slot = MakeSingleTupleTableSlot(RelationGetDescr(heapRelation));
	ExecStoreTuple(heapTuple, slot, InvalidBuffer, false);

	/*
	 * for each index, form and delete the index tuple
	 */
	for (i = 0; i < numIndexes; i++)
	{
		/*
		 * No need to update K2PG primary key which is intrinsic part of
		 * the base table.
		 */
		if (IsK2PgEnabled() && relationDescs[i]->rd_index->indisprimary)
			continue;

		IndexInfo  *indexInfo;

		indexInfo = indexInfoArray[i];

		/* If the index is marked as read-only, ignore it */
		if (!indexInfo->ii_ReadyForInserts)
			continue;

		/*
		 * Expressional and partial indexes on system catalogs are not
		 * supported, nor exclusion constraints, nor deferred uniqueness
		 */
		Assert(indexInfo->ii_Expressions == NIL);
		Assert(indexInfo->ii_Predicate == NIL);
		Assert(indexInfo->ii_ExclusionOps == NULL);
		Assert(relationDescs[i]->rd_index->indimmediate);

		/*
		 * FormIndexDatum fills in its values and isnull parameters with the
		 * appropriate values for the column(s) of the index.
		 */
		FormIndexDatum(indexInfo,
					   slot,
					   NULL,	/* no expression eval to do */
					   values,
					   isnull);

		/*
		 * The index AM does the rest.
		 */
        ItemPointer t_self = IsK2PgRelation(relationDescs[i]) ? (ItemPointer)(heapTuple->t_k2pgctid) : &(heapTuple->t_self);
        if (IsK2PgRelation(relationDescs[i])) {
            K2PgDeleteIndexRowsByBaseK2Pgctid(relationDescs[i], (Datum)t_self);
        } else {
            index_delete(relationDescs[i],	/* index relation */
                         values,	/* array of index Datums */
                         isnull,	/* is-null flags */
                         t_self);
        }
	}

	ExecDropSingleTupleTableSlot(slot);
}

void CatalogTupleDelete(Relation heapRel, HeapTuple tup)
{
    if (IsK2PgRelation(heapRel)) {
        K2PgDeleteSysCatalogTuple(heapRel, tup);
		if (K2PgRelHasSecondaryIndices(heapRel)) {
			CatalogIndexState indstate = CatalogOpenIndexes(heapRel);
			CatalogIndexDelete(indstate, tup);
			CatalogCloseIndexes(indstate);
		}
    } else {
        simple_heap_delete(heapRel, &tup->t_self);
    }
}

/*
 * CatalogUpdateIndexes - do all the indexing work for a new catalog tuple
 *
 * This is a convenience routine for the common case where we only need
 * to insert or update a single tuple in a system catalog.	Avoid using it for
 * multiple tuples, since opening the indexes and building the index info
 * structures is moderately expensive.
 */
void CatalogUpdateIndexes(Relation heapRel, HeapTuple heapTuple)
{
    CatalogIndexState indstate;

    indstate = CatalogOpenIndexes(heapRel);
	if (IsK2PgEnabled())
	{
		bool		has_indices = K2PgRelHasSecondaryIndices(heapRel);
		if (has_indices)
		{
			if (heapTuple->t_k2pgctid)
			{
				CatalogIndexDelete(indstate, heapTuple);
			}
			else
				elog(WARNING, "k2pgctid missing in %s's tuple",
								RelationGetRelationName(heapRel));
		}

		/* Update the local cache automatically */
		K2PgSetSysCacheTuple(heapRel, heapTuple);

		if (has_indices)
			CatalogIndexInsert(indstate, heapTuple);
	} else {
        CatalogIndexInsert(indstate, heapTuple);
    }

    CatalogCloseIndexes(indstate);
}

Oid CatalogTupleInsert(Relation heapRel, HeapTuple tup)
{
	Oid			oid;

	if (IsK2PgRelation(heapRel)) {
		oid = K2PgExecuteInsert(heapRel, RelationGetDescr(heapRel), tup);
		K2PgSetSysCacheTuple(heapRel, tup);
	} else {
		oid = simple_heap_insert(heapRel, tup);
	}

	return oid;
}

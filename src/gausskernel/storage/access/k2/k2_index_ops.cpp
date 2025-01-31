/*
MIT License

Copyright(c) 2022 Futurewei Cloud

    Permission is hereby granted,
    free of charge, to any person obtaining a copy of this software and associated documentation files(the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and / or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions :

    The above copyright notice and this permission notice shall be included in all copies
    or
    substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS",
    WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
    DAMAGES OR OTHER
    LIABILITY,
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

#include "postgres.h"

#include "miscadmin.h"
#include "access/nbtree.h"
#include "access/relscan.h"
#include "access/sysattr.h"
#include "access/k2/k2catam.h"
#include "access/k2/k2_index_ops.h"
#include "access/k2/k2cat_cmds.h"
#include "access/k2/k2_table_ops.h"
#include "catalog/index.h"
#include "catalog/pg_type.h"
#include "utils/rel.h"

/* --------------------------------------------------------------------------------------------- */

/* Working state for k2inbuild and its callback */
typedef struct
{
	bool	isprimary;		/* are we building a primary index? */
	double	index_tuples;	/* # of tuples inserted into index */
	bool	is_backfill;	/* are we concurrently backfilling an index? */
} K2PgBuildState;

static void k2inbuildCallback(Relation index, HeapTuple heapTuple, Datum *values, const bool *isnull,
				   bool tupleIsAlive, void *state)
{
	K2PgBuildState  *buildstate = (K2PgBuildState *)state;

	if (!buildstate->isprimary)
		K2PgExecuteInsertIndex(index,
							  values,
							  (bool *)isnull,
							  heapTuple->t_k2pgctid);

	buildstate->index_tuples += 1;
}

bool
k2invalidate(Oid opclassoid)
{
	return true;
}

Datum k2inbuild(PG_FUNCTION_ARGS)
{
	Relation heap = (Relation)PG_GETARG_POINTER(0);
    Relation index = (Relation)PG_GETARG_POINTER(1);
    IndexInfo *indexInfo = (IndexInfo *)PG_GETARG_POINTER(2);
	K2PgBuildState	buildstate;
	double			heap_tuples = 0;

	/* Do the heap scan */
	buildstate.isprimary = index->rd_index->indisprimary;
	buildstate.index_tuples = 0;
	buildstate.is_backfill = false;
	heap_tuples = IndexBuildHeapScan(heap, index, indexInfo, true, k2inbuildCallback,
									 &buildstate, NULL);

	/*
	 * Return statistics
	 */
	IndexBuildResult *result = (IndexBuildResult *) palloc(sizeof(IndexBuildResult));
	result->heap_tuples  = heap_tuples;
	result->index_tuples = buildstate.index_tuples;

	PG_RETURN_POINTER(result);
}

Datum k2inbuildempty(PG_FUNCTION_ARGS)
{
	Relation index = (Relation)PG_GETARG_POINTER(0);
	elog(WARNING, "Unexpected building of empty unlogged index: %d", index->rd_id);
	PG_RETURN_VOID();
}

Datum k2ininsert(PG_FUNCTION_ARGS)
{
	Relation index = (Relation)PG_GETARG_POINTER(0);
	Datum *values = (Datum *)PG_GETARG_POINTER(1);
    bool *isnull = (bool *)PG_GETARG_POINTER(2);
    Datum k2pgctid = (Datum)PG_GETARG_POINTER(3);
	Relation heap = (Relation)PG_GETARG_POINTER(4);
    IndexUniqueCheck checkUnique = (IndexUniqueCheck)PG_GETARG_INT32(5);
    IndexInfo *indexInfo = (IndexInfo *)PG_GETARG_POINTER(6);
    bool result = false;

	if (!index->rd_index->indisprimary)
		K2PgExecuteInsertIndex(index,
							  values,
							  isnull,
							  k2pgctid);

	result = index->rd_index->indisunique ? true : false;
	PG_RETURN_BOOL(result);
}

Datum k2indelete(PG_FUNCTION_ARGS)
{
	Relation index = (Relation)PG_GETARG_POINTER(0);
	Datum *values = (Datum *)PG_GETARG_POINTER(1);
    bool *isnull = (bool *)PG_GETARG_POINTER(2);
    Datum k2pgctid = (Datum)PG_GETARG_POINTER(3);
	Relation heap = (Relation)PG_GETARG_POINTER(4);
    IndexInfo *indexInfo = (IndexInfo *)PG_GETARG_POINTER(5);

	if (!index->rd_index->indisprimary)
		K2PgExecuteDeleteIndex(index, values, isnull, k2pgctid);

    PG_RETURN_VOID();
}

Datum k2inbulkdelete(PG_FUNCTION_ARGS)
{
	IndexVacuumInfo *info = (IndexVacuumInfo *)PG_GETARG_POINTER(0);
    IndexBulkDeleteResult *volatile stats = (IndexBulkDeleteResult *)PG_GETARG_POINTER(1);
    IndexBulkDeleteCallback callback = (IndexBulkDeleteCallback)PG_GETARG_POINTER(2);
    void *callbackState = (void *)PG_GETARG_POINTER(3);

	elog(WARNING, "Unexpected bulk delete of index via vacuum");

    PG_RETURN_POINTER(stats);
}

Datum k2invacuumcleanup(PG_FUNCTION_ARGS)
{
	IndexVacuumInfo *info = (IndexVacuumInfo *)PG_GETARG_POINTER(0);
    IndexBulkDeleteResult *stats = (IndexBulkDeleteResult *)PG_GETARG_POINTER(1);

	elog(WARNING, "Unexpected index cleanup via vacuum");

	PG_RETURN_POINTER(stats);
}

Datum k2incanreturn(PG_FUNCTION_ARGS)
{
	Relation index = (Relation)PG_GETARG_POINTER(0);
	int attno =  (int)PG_GETARG_INT32(1);
	/*
	 * If "canreturn" is true, Postgres will attempt to perform index-only scan on the indexed
	 * columns and expect us to return the column values as an IndexTuple. This will be the case
	 * for secondary index.
	 *
	 * For indexes which are primary keys, we will return the table row as a HeapTuple instead.
	 * For this reason, we set "canreturn" to false for primary keys.
	 */
	bool result = !index->rd_index->indisprimary;
	PG_RETURN_BOOL(result);
}

Datum k2incostestimate(PG_FUNCTION_ARGS)
{
    PlannerInfo* root = (PlannerInfo*)PG_GETARG_POINTER(0);
    IndexPath* path = (IndexPath*)PG_GETARG_POINTER(1);
    double loop_count = PG_GETARG_FLOAT8(2);
    Cost* indexStartupCost = (Cost*)PG_GETARG_POINTER(3);
    Cost* indexTotalCost = (Cost*)PG_GETARG_POINTER(4);
    Selectivity* indexSelectivity = (Selectivity*)PG_GETARG_POINTER(5);
    double* indexCorrelation = (double*)PG_GETARG_POINTER(6);

	camIndexCostEstimate(path, indexSelectivity, indexStartupCost, indexTotalCost);

	PG_RETURN_VOID();
}

Datum k2inoptions(PG_FUNCTION_ARGS)
{
	Datum reloptions = PG_GETARG_DATUM(0);
    bool validate = PG_GETARG_BOOL(1);
    bytea *result = NULL;

    if (result != NULL) {
        PG_RETURN_BYTEA_P(result);
    }
    PG_RETURN_NULL();
}

Datum k2inbeginscan(PG_FUNCTION_ARGS)
{
    Relation rel = (Relation)PG_GETARG_POINTER(0);
    int nkeys = PG_GETARG_INT32(1);
    int norderbys = PG_GETARG_INT32(2);

    IndexScanDesc scan;

    /* no order by operators allowed */
    Assert(norderbys == 0);

    /* get the scan */
    scan = RelationGetIndexScan(rel, nkeys, norderbys);
    scan->opaque = NULL;

    PG_RETURN_POINTER(scan);
}

Datum k2inrescan(PG_FUNCTION_ARGS)
{
	IndexScanDesc scan = (IndexScanDesc)PG_GETARG_POINTER(0);
    ScanKey scankey = (ScanKey)PG_GETARG_POINTER(1);
	int nscankeys = (int)PG_GETARG_INT32(2);
	ScanKey orderbys = (ScanKey)PG_GETARG_POINTER(3);
	int norderbys = (int)PG_GETARG_INT32(4);

	if (scan->opaque)
	{
		/* For rescan, end the previous scan. */
		Datum args[1];
		FunctionCallInfoData finfo;
		finfo.arg = &args[0];
		args[0] = PointerGetDatum(scan);
		k2inendscan(&finfo);
		scan->opaque = NULL;
	}

	CamScanDesc camScan = camBeginScan(scan->heapRelation, scan->indexRelation, scan->xs_want_itup,
																	 nscankeys, scankey);
	camScan->index = scan->indexRelation;
	scan->opaque = camScan;

	PG_RETURN_VOID();
}

Datum k2inendscan(PG_FUNCTION_ARGS)
{
	IndexScanDesc scan = (IndexScanDesc)PG_GETARG_POINTER(0);

	CamScanDesc k2can = (CamScanDesc)scan->opaque;
	Assert(PointerIsValid(k2can));
	camEndScan(k2can);

	PG_RETURN_VOID();
}

/*
 * Processing the following SELECT.
 *   SELECT data FROM heapRelation WHERE rowid IN
 *     ( SELECT rowid FROM indexRelation WHERE key = given_value )
 *
 */
Datum k2ingettuple(PG_FUNCTION_ARGS)
{
	IndexScanDesc scan = (IndexScanDesc)PG_GETARG_POINTER(0);
    ScanDirection dir = (ScanDirection)PG_GETARG_INT32(1);

	Assert(dir == ForwardScanDirection || dir == BackwardScanDirection);
	const bool is_forward_scan = (dir == ForwardScanDirection);

	CamScanDesc k2can = (CamScanDesc) scan->opaque;
	k2can->exec_params = scan->k2pg_exec_params;
	if (!is_forward_scan && !k2can->exec_params.limit_use_default) {
		// Ignore limit count for reverse scan since K2 PG cannot push down the limit for reverse scan and
		// rely on PG to process the limit count
		// this only applies if limit_use_default is not true
		k2can->exec_params.limit_count = -1;
	}
	Assert(PointerIsValid(k2can));

	/*
	 * IndexScan(SysTable, Index) --> HeapTuple.
	 */
	scan->xs_ctup.t_k2pgctid = 0;
	if (k2can->prepare_params.index_only_scan)
	{
		IndexTuple tuple = cam_getnext_indextuple(k2can, is_forward_scan, &scan->xs_recheck);
		if (tuple)
		{
			scan->xs_ctup.t_k2pgctid = tuple->t_k2pgctid;
			scan->xs_itup = tuple;
			scan->xs_itupdesc = RelationGetDescr(scan->indexRelation);
		}
	}
	else
	{
		HeapTuple tuple = cam_getnext_heaptuple(k2can, is_forward_scan, &scan->xs_recheck);
		if (tuple)
		{
			scan->xs_ctup.t_k2pgctid = tuple->t_k2pgctid;
			scan->xs_hitup = tuple;
			scan->xs_hitupdesc = RelationGetDescr(scan->heapRelation);
		}
	}

	bool result = scan->xs_ctup.t_k2pgctid != 0;

	PG_RETURN_BOOL(result);
}

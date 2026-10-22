#include "c.h"
#include "postgres.h"

#include "utils/elog.h"
#include "fmgr.h"
#include "nodes/pathnodes.h"
#include "optimizer/paths.h"
#include "optimizer/plancat.h"
#include "optimizer/planner.h"
#include "executor/execdesc.h"
#include "nodes/pg_list.h"
#include "parser/analyze.h"
#include "nodes/plannodes.h"
#include "nodes/params.h"
#include "executor/executor.h"
#include "nodes/nodes.h"
#include "utils/hsearch.h"
#include "utils/memutils.h"
#include "utils/palloc.h"
#include "utils/guc.h"
#include "optimizer/cost.h"
#include "miscadmin.h"

#include <postgresql/18/server/access/relation.h>
#include <postgresql/18/server/catalog/catalog.h>
#include <postgresql/18/server/nodes/parsenodes.h>
#include <stdbool.h>
#include <string.h>

// Other includes from `pg_hint_plan`

// #include "access/heapam.h"
// #include "access/relation.h"
// #include "catalog/pg_collation.h"
// #include "catalog/pg_index.h"
// #include "catalog/pg_proc.h"
// #include "commands/prepare.h"
// #include "commands/proclang.h"
// #include "mb/pg_wchar.h"
// #include "nodes/nodeFuncs.h"
// #include "optimizer/appendinfo.h"
// #include "optimizer/clauses.h"
// #include "optimizer/geqo.h"
// #include "optimizer/joininfo.h"
// #include "optimizer/optimizer.h"
// #include "optimizer/pathnode.h"
// #include "optimizer/prep.h"
// #include "optimizer/restrictinfo.h"
// #include "parser/parsetree.h"
// #include "parser/scansup.h"
// #include "partitioning/partbounds.h"
// #include "tcop/utility.h"
// #include "utils/builtins.h"
// #include "utils/float.h"
// #include "utils/lsyscache.h"
// #include "utils/memutils.h"
// #include "utils/rel.h"
// #include "utils/snapmgr.h"
// #include "utils/syscache.h"
// #include "catalog/pg_class.h"
// #include "executor/spi.h"
// #include "catalog/pg_type.h"

PG_MODULE_MAGIC;
void _PG_init (void);

// -----------------------------------------------------------------------------
//                                      STRUCTS
// -----------------------------------------------------------------------------

static int**    changes;

static char**   scan_enables;
static int      pointer;
static bool     is_system;

// -----------------------------------------------------------------------------
//                                         GUC
// -----------------------------------------------------------------------------

static bool force_hashjoin  = false;
static bool force_mergejoin = false;
static bool force_nestloop  = false;

// -----------------------------------------------------------------------------
//                                   HOOKS - VARS
// -----------------------------------------------------------------------------

static join_search_hook_type            prev_join_search = NULL;
static post_parse_analyze_hook_type     prev_post_parse_analyzer_hook = NULL;
static planner_hook_type                prev_planner = NULL;
static set_rel_pathlist_hook_type       prev_set_rel_pathlist = NULL;
static get_relation_info_hook_type      prev_get_relation_info_hook = NULL;
static ExecutorEnd_hook_type            prev_ExecutorEnd = NULL;

// -----------------------------------------------------------------------------
//                          EXT. PROCEDURES - DECLARATIONS
// -----------------------------------------------------------------------------

static int          pg_plan_fuzz_set_config_option(
                    const char* value,
                    const char* name
                    );
static const char*  pg_plan_fuzz_node_type_to_str    (int type);
static void         pg_plan_fuzz_join_transformation (int type); 
static void         pg_plan_fuzz_generate_changes    (RelOptInfo *relinfo);
static void         pg_plan_fuzz_init_scan_enables   ();
static void         pg_plan_fuzz_check_system_table (Oid relationObjectId);

// -----------------------------------------------------------------------------
//                          EXT. PROCEDURES - IMPLEMENTATIONS
// -----------------------------------------------------------------------------

static void pg_plan_fuzz_check_system_table (Oid relationObjectOid) 
{
    Relation    rel;

    is_system = false;
        
    if (OidIsValid(relationObjectOid)) 
    {
        rel = relation_open(relationObjectOid, NoLock);
        if (IsSystemRelation(rel)) { is_system = true; }
        relation_close(rel, NoLock);
    }
}

static void pg_plan_fuzz_join_transformation (int type) 
{
    switch (type) 
    {
        case T_HashJoin: 
        {
            pg_plan_fuzz_set_config_option (
                "enable_nestloop", 
                "off");
            pg_plan_fuzz_set_config_option (
                "enable_mergejoin",
                "off"
            ); return;
        };
        case T_MergeJoin: 
        {
            pg_plan_fuzz_set_config_option (
                "enable_hashjoin",
                "off"
            );
            pg_plan_fuzz_set_config_option (
                "enable_nestloop",
                "off"
            ); return;
        }
        case T_NestLoop: 
        {
            pg_plan_fuzz_set_config_option (
                "enable_hashjoin",
                "off"
            );
            pg_plan_fuzz_set_config_option(
                "enable_mergejoin",
                "off"
            ); return;
        }
        default:
        { return; }
    }
}

static void pg_plan_fuzz_generate_changes (RelOptInfo *relinfo) 
{
    ListCell    *lc;
    int         all_variants;
    
    foreach (lc, relinfo->pathlist) 
    {
        
    }

}

static void pg_plan_fuzz_init_scan_enables () 
{
    int             size;
    MemoryContext   ccxt;

    ccxt         = CurrentMemoryContext;

    MemoryContextSwitchTo(TopMemoryContext);

    size         = 4;

    scan_enables = (char**)palloc0(sizeof(char*) * size);

    scan_enables[0] = (char*)palloc0(sizeof(char) * 30);
    scan_enables[1] = (char*)palloc0(sizeof(char) * 30);
    scan_enables[2] = (char*)palloc0(sizeof(char) * 30);
    scan_enables[3] = (char*)palloc0(sizeof(char) * 30);

    scan_enables[0] = "enable_seqscan";
    scan_enables[1] = "enable_indexscan";
    scan_enables[2] = "enable_indexonlyscan";
    scan_enables[3] = "enable_bitmapscan";

    pointer         = 0;

    MemoryContextSwitchTo(ccxt);
}

static const char* pg_plan_fuzz_node_type_to_str(int type)
{
    switch (type) {
        case T_SeqScan:         return "SeqScan";
        case T_IndexScan:       return "IndexScan";
        case T_IndexOnlyScan:   return "IndexOnlyScan";
        case T_BitmapHeapScan:  return "BitmapHeapScan";
        case T_BitmapIndexScan: return "BitmapIndexScan";
        case T_HashJoin:        return "HashJoin";
        case T_NestLoop:        return "NestLoop";
        case T_MergeJoin:       return "MergeJoin";
        case T_Sort:            return "Sort";
        case T_Agg:             return "Agg";
        case T_Result:          return "Result";
        case T_Limit:           return "Limit";
        case T_SubqueryScan:    return "SubqueryScan";
        default:                return "Other Node";
    }
}

// This is function for changing current GUC's state
// This realization i get from `pg_hint_plan` extension
// I will use it for enabling hooks in postgresql

static int pg_plan_fuzz_set_config_option (
            const char* name,
            const char* value
            ) 
{
    int             result; 
    MemoryContext   ccxt;
    GucContext      state;

    result  = 0;
    ccxt    = CurrentMemoryContext;
    state   = superuser() ? PGC_SUSET : PGC_USERSET;

    PG_TRY();
    {
        result = set_config_option(
            name,
            value,
            state,
            PGC_S_SESSION,
            GUC_ACTION_SAVE,
            true,
            0,
            false
        );
    }
    PG_CATCH();
    {
        ErrorData *errdata;

        MemoryContextSwitchTo(ccxt);
        errdata = CopyErrorData();
        FlushErrorState();

        ereport(
            INFO,
            (errcode(errdata->sqlerrcode),
             errmsg("%s", errdata->message),
             errdata->detail ? errdetail("%s", errdata->hint) : 0)
            );
        
        FreeErrorData(errdata);
    }
    PG_END_TRY();

    return result;
}

// -----------------------------------------------------------------------------
//                            HOOKS - STATIC DECLARATIONS
// -----------------------------------------------------------------------------

static RelOptInfo *pg_plan_fuzz_join_search (
    PlannerInfo *root,
    int         levels_needed,
    List        *initial_rels
);

static void pg_plan_fuzz_post_parse_anlayze (
    ParseState  *pstate,
    Query       *query,
    JumbleState *jstate
);

static PlannedStmt *pg_plan_fuzz_planner (
    Query           *parse, 
    const char      *query_string, 
    int             cursorOptions,				 
    ParamListInfo   boundParams
);

static void pg_plan_fuzz_set_rel_pathlist (
    PlannerInfo     *root,
    RelOptInfo      *rel,
    Index           rti,
    RangeTblEntry   *rte
);

static void pg_plan_fuzz_relation_info_hook (
    PlannerInfo     *root,
    Oid             relationObjectid,
    bool            inhparent, 
    RelOptInfo      *rel
);

static void pg_plan_fuzz_ExecutorEnd (
    QueryDesc *queryDesc
);

// -----------------------------------------------------------------------------
//                              HOOKS - IMPLEMENTATIONS
// -----------------------------------------------------------------------------

static RelOptInfo *pg_plan_fuzz_join_search (
    PlannerInfo     *root,
    int             levels_needed,
    List            *initial_rels
) 
{
    RelOptInfo  *rel;
    ListCell    *lc;
    Path        *path;

    if (prev_join_search)   rel = prev_join_search(
        root,
        levels_needed,
        initial_rels
    );

    else                    rel = standard_join_search(
        root, 
        levels_needed,
        initial_rels
    );

    if (is_system)
        return rel;
    
    foreach (lc, rel->pathlist) 
    {
        path = (Path*)lfirst(lc);

        elog(
            INFO,
            "Join node: %s \t\t -> COST -- %f --",
            pg_plan_fuzz_node_type_to_str(path->pathtype),
            path->total_cost
        );
    }

    return rel;
}

static void pg_plan_fuzz_post_parse_anlayze (
    ParseState  *pstate,
    Query       *query,
    JumbleState *jstate
) 
{
    if (prev_post_parse_analyzer_hook)  prev_post_parse_analyzer_hook(
        pstate,
        query,
        jstate
    );
}

static PlannedStmt *pg_plan_fuzz_planner (
    Query           *parse,
    const char      *query_string,
    int             cursorOptions,
    ParamListInfo   boundParams
) 
{
    PlannedStmt *result;

    if (prev_planner)   result = prev_planner(
        parse,
        query_string,
        cursorOptions,
        boundParams
    );

    else                result = standard_planner(
        parse, 
        query_string, 
        cursorOptions, 
        boundParams
    );

    return result;
}

static void pg_plan_fuzz_set_rel_pathlist (
    PlannerInfo     *root,
    RelOptInfo      *rel,
    Index           rti,
    RangeTblEntry   *rte
) 
{
    ListCell    *lc;
    Path        *path;

    if (prev_set_rel_pathlist)  prev_set_rel_pathlist(
        root,
        rel,
        rti,
        rte
    );

    // Here we will place Join Transforms, because in 
    // this current state of plan optimizer still not 
    // change any join node

    if (force_hashjoin)    pg_plan_fuzz_join_transformation(T_HashJoin);
    if (force_mergejoin)   pg_plan_fuzz_join_transformation(T_MergeJoin);
    if (force_nestloop)    pg_plan_fuzz_join_transformation(T_NestLoop);

    if (is_system) 
        return;

    foreach (lc, rel->pathlist) 
    {
        path = (Path*)lfirst(lc);

        elog(
            INFO,
            "Scan node: %s \t\t -> COST -- %f --",
            pg_plan_fuzz_node_type_to_str(path->pathtype),
            path->total_cost
        );
    }
}

static void pg_plan_fuzz_relation_info_hook (
    PlannerInfo     *root,
    Oid             relationObjectId,
    bool            inhparent, 
    RelOptInfo      *rel
) 
{
    if (prev_get_relation_info_hook)    prev_get_relation_info_hook(
        root,
        relationObjectId,
        inhparent,
        rel
    );

    pg_plan_fuzz_check_system_table(relationObjectId);
    
}

static void pg_plan_fuzz_ExecutorEnd (
    QueryDesc *queryDesc
) 
{
    if (prev_ExecutorEnd)   prev_ExecutorEnd(queryDesc);
    else                    standard_ExecutorEnd(queryDesc);
}

void _PG_init (void) 
{
    elog(INFO, "Initialized pg_plan_fuzz extension");

    pg_plan_fuzz_init_scan_enables();

    DefineCustomBoolVariable(
        "pg_plan_fuzz.force_hashjoin", 
        "Force planner/optimizer to use HashJoin in plan",
        NULL, 
        &force_hashjoin,
        false,
        PGC_USERSET,
        0,
        NULL,
        NULL,
        NULL
    );

    DefineCustomBoolVariable(
        "pg_plan_fuzz.force_mergejoin", 
        "Force planner/optimizer to use MergeJoin in plan",
        NULL, 
        &force_mergejoin,
        false,
        PGC_USERSET,
        0,
        NULL,
        NULL,
        NULL
    );

    DefineCustomBoolVariable(
        "pg_plan_fuzz.force_nestloop", 
        "Force planner/optimizer to use NestedLoop in plan",
        NULL, 
        &force_nestloop,
        false,
        PGC_USERSET,
        0,
        NULL,
        NULL,
        NULL
    );

    prev_join_search                        = join_search_hook;
    join_search_hook                        = pg_plan_fuzz_join_search;
    prev_post_parse_analyzer_hook           = post_parse_analyze_hook;
    post_parse_analyze_hook                 = pg_plan_fuzz_post_parse_anlayze;
    prev_planner                            = planner_hook;
    planner_hook                            = pg_plan_fuzz_planner;
    prev_set_rel_pathlist                   = set_rel_pathlist_hook;
    set_rel_pathlist_hook                   = pg_plan_fuzz_set_rel_pathlist;
    prev_get_relation_info_hook             = get_relation_info_hook;
    get_relation_info_hook                  = pg_plan_fuzz_relation_info_hook;
    prev_ExecutorEnd                        = ExecutorEnd_hook;
    ExecutorEnd_hook                        = pg_plan_fuzz_ExecutorEnd;
}

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

// #include "access/heapam.h"
// #include "access/relation.h"
// #include "catalog/pg_collation.h"
// #include "catalog/pg_index.h"
// #include "catalog/pg_proc.h"
// #include "commands/prepare.h"
// #include "commands/proclang.h"
// #include "mb/pg_wchar.h"
// #include "miscadmin.h"
// #include "nodes/nodeFuncs.h"
// #include "optimizer/appendinfo.h"
// #include "optimizer/clauses.h"
// #include "optimizer/cost.h"
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

// -----------------------------------------------------------------------------------
//                                   HOOKS - VARS
// -----------------------------------------------------------------------------------

static join_search_hook_type            prev_join_search = NULL;
static post_parse_analyze_hook_type     prev_post_parse_analyzer_hook = NULL;
static planner_hook_type                prev_planner = NULL;
static set_rel_pathlist_hook_type       prev_set_rel_pathlist = NULL;
static get_relation_info_hook_type      prev_get_relation_info_hook = NULL;
static ExecutorEnd_hook_type            prev_ExecutorEnd = NULL;

// -----------------------------------------------------------------------------------
//                            HOOKS - STATIC DECLARATIONS
// -----------------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------------
//                              HOOKS - IMPLEMENTATIONS
// -----------------------------------------------------------------------------------

static RelOptInfo *pg_plan_fuzz_join_search(
    PlannerInfo     *root,
    int             levels_needed,
    List            *initial_rels
) 
{
    RelOptInfo *rel;

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

    return rel;
}

static void pg_plan_fuzz_post_parse_anlayze(
    ParseState  *pstate,
    Query       *query,
    JumbleState *jstate
) 
{}

static PlannedStmt *pg_plan_fuzz_planner(
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

static void pg_plan_fuzz_set_rel_pathlist(
    PlannerInfo     *root,
    RelOptInfo      *rel,
    Index           rti,
    RangeTblEntry   *rte
) 
{}

static void pg_plan_fuzz_relation_info_hook(
    PlannerInfo     *root,
    Oid             relationObjectid,
    bool            inhparent, 
    RelOptInfo      *rel
) 
{}

static void pg_plan_fuzz_ExecutorEnd (
    QueryDesc *queryDesc
) 
{}

void _PG_init(void);
void _PG_init(void) 
{
    elog(INFO, "Initialized pg_plan_fuzz extension");

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


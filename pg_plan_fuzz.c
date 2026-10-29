#include <string.h>

#include "postgres.h"
#include "access/genam.h"
#include "access/heapam.h"
#include "access/relation.h"
#include "catalog/namespace.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_index.h"
#include "catalog/pg_proc.h"
#include "commands/prepare.h"
#include "commands/proclang.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "nodes/nodeFuncs.h"
#include "nodes/params.h"
#include "optimizer/appendinfo.h"
#include "optimizer/clauses.h"
#include "optimizer/cost.h"
#include "optimizer/geqo.h"
#include "optimizer/joininfo.h"
#include "optimizer/optimizer.h"
#include "optimizer/pathnode.h"
#include "optimizer/paths.h"
#include "optimizer/plancat.h"
#include "optimizer/planner.h"
#include "optimizer/prep.h"
#include "optimizer/restrictinfo.h"
#include "parser/analyze.h"
#include "parser/parsetree.h"
#include "parser/scansup.h"
#include "partitioning/partbounds.h"
#include "tcop/utility.h"
#include "utils/backend_status.h"
#include "utils/builtins.h"
#include "utils/float.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"

#include "catalog/pg_class.h"

#include "executor/spi.h"
#include "catalog/pg_type.h"

#include "plpgsql.h"

#define JOIN_PATHLIST           0
#define JOIN_SEARCH             1
#define NO                      100000000.00
#define YES                     0.0000000001

typedef unsigned int ** BITMATRIX;
typedef unsigned int *  BITROW;
static const int MAX_JOIN_RECOURSE_LEVEL = 1000000;

PG_MODULE_MAGIC;
void _PG_init (void);

// -----------------------------------------------------------------------------
//                                      STRUCTS
// -----------------------------------------------------------------------------

static BITMATRIX    join_bitmatrix;

static char**       scan_enables;
static int          pointer;
static bool*        is_system;
static int          join_recourse_level_    = 1;
static bool         multirelation           = false;
static int          relations_counter       = 0;
static List*        current_pathlist        = NIL;
static List*        list_of_joinrelids      = NIL;
static Relids       current_joinrelids      = NULL;

// -----------------------------------------------------------------------------
//                                         GUC
// -----------------------------------------------------------------------------

static bool     force_hashjoin      = false;
static bool     force_mergejoin     = false;
static bool     force_nestloop      = false;
static int      join_recourse_level = 0;

// -----------------------------------------------------------------------------
//                                   HOOKS - VARS
// -----------------------------------------------------------------------------

static join_search_hook_type            prev_join_search = NULL;
static post_parse_analyze_hook_type     prev_post_parse_analyzer_hook = NULL;
static planner_hook_type                prev_planner = NULL;
static set_rel_pathlist_hook_type       prev_set_rel_pathlist = NULL;
static get_relation_info_hook_type      prev_get_relation_info_hook = NULL;
static ExecutorEnd_hook_type            prev_ExecutorEnd = NULL;
static set_join_pathlist_hook_type      prev_join_pathlist_hook = NULL;

// -----------------------------------------------------------------------------
//                          EXT. PROCEDURES - DECLARATIONS
// -----------------------------------------------------------------------------

static int          pg_plan_fuzz_set_config_option(
                    const char* value,
                    const char* name
                    );
static const char*  pg_plan_fuzz_node_type_to_str    (int type);

static void         pg_plan_fuzz_join_operator       ();
static void         pg_plan_fuzz_join_transformator  ();
static void         pg_plan_fuzz_join_navigator      (int pos);
static void         pg_plan_fuzz_join_coster         ();
static void         pg_plan_fuzz_join_set_relids     (Relids bms1, Relids bms2);
static void         pg_plan_fuzz_join_add_relids     ();
static void         pg_plan_fuzz_join_generator      ();

static void         pg_plan_fuzz_scan_spanner        (int type);
static void         pg_plan_fuzz_scan_operator       ();
static void         pg_plan_fuzz_init_scan_enables   ();
static void         pg_plan_fuzz_check_system_table  (Oid relationObjectId);
static void         pg_plan_fuzz_clear               ();
static void         pg_plan_fuzz_clear_join_enables  ();
static void         pg_plan_fuzz_init                ();
static void         pg_plan_fuzz_force_mergejoin     ();
static void         pg_plan_fuzz_force_hashjoin      ();
static void         pg_plan_fuzz_force_nestloop      ();

// -----------------------------------------------------------------------------
//                          CORE STUFF - DECLARATIONS
// -----------------------------------------------------------------------------

static void         create_plain_partial_paths       (
                    PlannerInfo *root,
                    RelOptInfo *rel
                    );
static bool         has_join_restriction             (
                    PlannerInfo *root, 
                    RelOptInfo *rel
                    );
static void         make_rels_by_clause_joins        (
                    PlannerInfo *root,
                    RelOptInfo *old_rel,
                    List *other_rels, 
                    int first_rel_idx
                    );   
static void         make_rels_by_clauseless_joins    (
                    PlannerInfo *root,
                    RelOptInfo *old_rel, 
                    List *other_rels
                    );
static void         free_child_join_sjinfo           (SpecialJoinInfo *sjinfo);
static void         populate_joinrel_with_paths      (
                    PlannerInfo *root,
                    RelOptInfo *rel1,
                    RelOptInfo *rel2,
                    RelOptInfo *joinrel,
                    SpecialJoinInfo *sjinfo,
                    List *restrictlist
                    );

static void         add_paths_to_joinrel_wrapper    (
                    PlannerInfo *root, 
                    RelOptInfo *joinrel,
                    RelOptInfo *outerrel, 
                    RelOptInfo *innerrel,
                    JoinType jointype, 
                    SpecialJoinInfo *sjinfo,             
                    List *restrictlist
                    );

static RelOptInfo   *pg_plan_fuzz_make_join_rel     (
                    PlannerInfo *root,
                    RelOptInfo *rel1,
                    RelOptInfo *rel2
                    );

// -----------------------------------------------------------------------------
//                          EXT. PROCEDURES - IMPLEMENTATIONS
// -----------------------------------------------------------------------------

static void pg_plan_fuzz_join_add_relids() 
{
    list_of_joinrelids = lappend(
        list_of_joinrelids,
        current_joinrelids);
}

static void pg_plan_fuzz_join_set_relids(Relids bms1, Relids bms2) 
{
    Relids joinrelids;
    joinrelids = bms_union(bms1, bms2);

    if (current_joinrelids == NULL)  
        current_joinrelids = joinrelids;

    if (!bms_equal(current_joinrelids, joinrelids))
    {
        pg_plan_fuzz_join_add_relids();
        pg_plan_fuzz_clear_join_enables();
        current_joinrelids = joinrelids;
        join_recourse_level_++;
    }
}

static void pg_plan_fuzz_join_coster() 
{
    ListCell    *lc;
    Path        *path;
    int         type;

    type = 0;

    if (force_hashjoin)     type = T_HashJoin;
    if (force_mergejoin)    type = T_MergeJoin;
    if (force_nestloop)     type = T_NestLoop;

    if (type == 0)              return;

    foreach (lc, current_pathlist) 
    {
        path = (Path*)lfirst(lc);

        if (path->pathtype == type)     path->total_cost = YES;
        else                            path->total_cost = NO;
    }
}

static void pg_plan_fuzz_join_navigator (int pos) 
{
    switch (pos) 
    {
        case JOIN_PATHLIST: 
        {
            if (multirelation) pg_plan_fuzz_join_transformator();
            return;
        }
        case JOIN_SEARCH:
        {
            if (!multirelation) pg_plan_fuzz_join_operator();
            if (multirelation && join_recourse_level == 0)
                pg_plan_fuzz_join_operator();
            return;
        }
        default: { return; }
    }
}

static void pg_plan_fuzz_join_transformator () 
{
    if (join_recourse_level_ == join_recourse_level) 
    { 
        elog (
            INFO,
            "join_recourse_level: \t%d",
            join_recourse_level
        );
        elog (
            INFO,
            "join_recourse_level_: \t%d",
            join_recourse_level_
        );
        pg_plan_fuzz_join_operator();
    }
}

static void pg_plan_fuzz_force_mergejoin () 
{
    pg_plan_fuzz_set_config_option("enable_mergejoin", "on");
    pg_plan_fuzz_set_config_option("enable_hash`join", "off");
    pg_plan_fuzz_set_config_option("enable_nestloop", "off");

    enable_mergejoin    = true;
    enable_hashjoin     = false;
    enable_nestloop     = false; 
}

static void pg_plan_fuzz_force_hashjoin () 
{
    pg_plan_fuzz_set_config_option("enable_mergejoin", "off");
    pg_plan_fuzz_set_config_option("enable_hashjoin", "on");
    pg_plan_fuzz_set_config_option("enable_nestloop", "off");

    enable_mergejoin    = false;
    enable_hashjoin     = true;
    enable_nestloop     = false; 
}

static void pg_plan_fuzz_force_nestloop () 
{
    pg_plan_fuzz_set_config_option("enable_mergejoin", "off");
    pg_plan_fuzz_set_config_option("enable_hashjoin", "off");
    pg_plan_fuzz_set_config_option("enable_nestloop", "on");

    enable_mergejoin    = false;
    enable_hashjoin     = false;
    enable_nestloop     = true; 
}

// Actually we need to revert state of GUC's
// But i do not thnik that is needed in our case
// It is not nessecary for fuzzer

static void pg_plan_fuzz_clear_join_enables () 
{
    pg_plan_fuzz_set_config_option("enable_hashjoin", "on");
    pg_plan_fuzz_set_config_option("enable_mergejoin", "on");
    pg_plan_fuzz_set_config_option("enable_nestloop", "on");
}

static void pg_plan_fuzz_join_operator () 
{
    if (force_hashjoin)     pg_plan_fuzz_force_hashjoin(); 
    if (force_mergejoin)    pg_plan_fuzz_force_mergejoin();
    if (force_nestloop)     pg_plan_fuzz_force_nestloop();
}

static void pg_plan_fuzz_init () 
{
    MemoryContext ccxt;
    ccxt        = CurrentMemoryContext;

    MemoryContextSwitchTo(TopMemoryContext);

    is_system   = (bool*)palloc0(sizeof(bool));
    *is_system  = false;

    join_bitmatrix = (BITMATRIX)palloc0(sizeof(BITMATRIX));

    MemoryContextSwitchTo(ccxt);
}

static void pg_plan_fuzz_clear() 
{
    join_recourse_level_    = 1;
    relations_counter       = 0;
    multirelation           = false;
    current_joinrelids      = NULL;
    list_of_joinrelids      = NIL;
    pg_plan_fuzz_clear_join_enables();
}

static void pg_plan_fuzz_check_system_table (Oid relationObjectOid) 
{
    Relation    rel;
        
    if (OidIsValid(relationObjectOid)) 
    {
        rel = relation_open(relationObjectOid, NoLock);
        if (IsSystemRelation(rel)) { *is_system = true; }
        relation_close(rel, NoLock);
    }
}

static void pg_plan_fuzz_join_generator () 
{
    unsigned int combinations;
    unsigned int pool;

    if (list_of_joinrelids){}

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

    if (*is_system)         return standard_join_search(
        root, 
        levels_needed,
        initial_rels
    );

    pg_plan_fuzz_join_navigator(JOIN_SEARCH);

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

    pg_plan_fuzz_clear();

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

    if (*is_system)
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
    
    if (*is_system)
        return;

    relations_counter++;
    if (relations_counter > 2) multirelation = true;
}

static void pg_plan_fuzz_ExecutorEnd (
    QueryDesc *queryDesc
) 
{
    if (prev_ExecutorEnd)   prev_ExecutorEnd(queryDesc);
    else                    standard_ExecutorEnd(queryDesc);
}

static void
add_paths_to_joinrel_wrapper(PlannerInfo *root,
							 RelOptInfo *joinrel,
							 RelOptInfo *outerrel,
							 RelOptInfo *innerrel,
							 JoinType jointype,
							 SpecialJoinInfo *sjinfo,
							 List *restrictlist)
{
	int				save_nestlevel;

    elog (INFO, "Call wraper");

    save_nestlevel = NewGUCNestLevel();

    pg_plan_fuzz_join_set_relids(
        outerrel->relids,
        innerrel->relids
    );

    pg_plan_fuzz_join_transformator();
    
    add_paths_to_joinrel(
        root, 
        joinrel, 
        outerrel, 
        innerrel, 
        jointype, 
        sjinfo, 
        restrictlist
    );

    current_pathlist = joinrel->pathlist;

    ListCell *lc;
    Path     *path;

    foreach (lc, joinrel->pathlist) 
    {   
        path = (Path*)lfirst(lc);
        elog (
            INFO, 
            "Path type: %s",
            pg_plan_fuzz_node_type_to_str(path->pathtype)
        );
    }

    pg_plan_fuzz_join_coster();

    AtEOXact_GUC(true, save_nestlevel);
}

void _PG_init (void) 
{
    elog(INFO, "Initialized pg_plan_fuzz extension");

    pg_plan_fuzz_init_scan_enables();
    pg_plan_fuzz_init();

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

    DefineCustomIntVariable(
        "pg_plan_fuzz.join_recourse_level",
        "Current query join_recourse_level",
        NULL,
        &join_recourse_level,
        0,
        0,
        MAX_JOIN_RECOURSE_LEVEL,
        PGC_USERSET,
        0,
        NULL,
        NULL,
        NULL);

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

#define make_join_rel pg_plan_fuzz_make_join_rel
#define add_paths_to_joinrel add_paths_to_joinrel_wrapper
#include "core.c"
#include "make_join_rel.c"
#undef make_join_rel
#undef add_paths_to_joinrel
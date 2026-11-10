
#include "postgres.h"

#include "catalog/catalog.h"
#include <stdbool.h>
#include <string.h>

#include "nodes/nodes.h"
#include "nodes/pathnodes.h"
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
#include "utils/guc.h"
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

bool current_enable_hashjoin        = true;
bool current_enable_mergejoin       = true;
bool current_enable_nestloop        = true;
bool current_enable_seqscan         = true;
bool current_enable_indexscan       = true;
bool current_enable_indexonlyscan   = true;


// -----------------------------------------------------------------------------
//                                      STRUCTS
// -----------------------------------------------------------------------------

static BITMATRIX    join_bitmatrix;

static char**       scan_enables;
static int          pointer;
static bool*        is_system;
static int          join_recourse_level_    = 1;
static int          scan_recourse_level_    = 1;
static bool         multirelation           = false;
static int          relations_counter       = 0;
static List*        current_join_pathlist   = NIL;
static List*        current_scan_pathlist   = NIL;
static List*        list_of_joinrelids      = NIL;
static Relids       current_joinrelids      = NULL;

// -----------------------------------------------------------------------------
//                                         GUC
// -----------------------------------------------------------------------------

static bool     force_hashjoin      = false;
static bool     force_mergejoin     = false;
static bool     force_nestloop      = false;
static bool     force_seqscan       = false;
static bool     force_indexscan     = false;
static bool     force_indexonlyscan = false;
static bool     force_bitmapscan    = false;
static int      join_recourse_level = 0;
static int      scan_recourse_level = 0;
static bool     logging_mode        = false;

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

static void         pg_plan_fuzz_join_operator       ();
static void         pg_plan_fuzz_join_transformator  ();
static void         pg_plan_fuzz_join_navigator      (int pos);
static void         pg_plan_fuzz_join_coster         ();
static void         pg_plan_fuzz_join_set_relids     (Relids bms1, Relids bms2);
static void         pg_plan_fuzz_join_add_relids     ();
static void         pg_plan_fuzz_join_generator      ();

static void         pg_plan_fuzz_scan_operator       ();
static void         pg_plan_fuzz_scan_coster         ();

static void         pg_plan_fuzz_init_scan_enables   ();
static void         pg_plan_fuzz_check_system_table  (Oid relationObjectId);
static void         pg_plan_fuzz_clear               ();
static void         pg_plan_fuzz_clear_join_enables  ();
static void         pg_plan_fuzz_clear_scan_enables  ();
static void         pg_plan_fuzz_init                ();

static void         pg_plan_fuzz_force_mergejoin     ();
static void         pg_plan_fuzz_force_hashjoin      ();
static void         pg_plan_fuzz_force_nestloop      ();

static void         pg_plan_fuzz_force_seqscan       ();
static void         pg_plan_fuzz_force_indexscan     ();
static void         pg_plan_fuzz_force_indexonlyscan ();
static void         pg_plan_fuzz_force_bitmapscan    ();

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

static void         pg_plan_fuzz_clear_scan_enables  () 
{
    pg_plan_fuzz_set_config_option("enable_seqscan", 
        current_enable_seqscan ? "on" : "off");
    pg_plan_fuzz_set_config_option("enable_indexscan", 
        current_enable_indexscan ? "on" : "off");
    pg_plan_fuzz_set_config_option("enable_indexonlyscan", 
        current_enable_indexonlyscan ? "on" : "off");
    pg_plan_fuzz_set_config_option("enable_bitmapscan", "on");
} 

static void         pg_plan_fuzz_scan_operator       () 
{
    if (force_seqscan)          pg_plan_fuzz_force_seqscan();
    if (force_indexscan)        pg_plan_fuzz_force_indexscan();
    if (force_indexonlyscan)    pg_plan_fuzz_force_indexonlyscan();
    if (force_bitmapscan)       pg_plan_fuzz_force_bitmapscan();
}

static void         pg_plan_fuzz_scan_coster         () 
{
    ListCell    *lc;
    Path        *path;
    NodeTag     type;

    type = 0;

    if (force_seqscan)          type = T_SeqScan;
    if (force_indexscan)        type = T_IndexScan;
    if (force_indexonlyscan)    type = T_IndexOnlyScan;
    if (force_bitmapscan)       type = T_BitmapHeapScan;

    if (!type) return;

    foreach (lc, current_scan_pathlist) 
    {
        path = (Path*)lfirst(lc);

        if (scan_recourse_level_ == scan_recourse_level) 
        {
            if (path->pathtype == type) path->total_cost = YES;
            else                        path->total_cost = NO;
        }
    }

}

static void         pg_plan_fuzz_force_seqscan       () 
{
    pg_plan_fuzz_set_config_option("enable_seqscan", "on");
    pg_plan_fuzz_set_config_option("enable_indexscan", "off");
    pg_plan_fuzz_set_config_option("enable_indexonlyscan", "off");
    pg_plan_fuzz_set_config_option("enable_bitmapscan", "off");

    enable_seqscan          = true;
    enable_indexscan        = false;
    enable_indexonlyscan    = false;
    enable_bitmapscan       = false;
}

static void         pg_plan_fuzz_force_indexscan     () 
{
    pg_plan_fuzz_set_config_option("enable_seqscan", "off");
    pg_plan_fuzz_set_config_option("enable_indexscan", "on");
    pg_plan_fuzz_set_config_option("enable_indexonlyscan", "off");
    pg_plan_fuzz_set_config_option("enable_bitmapscan", "off");

    enable_seqscan          = false;
    enable_indexscan        = true;
    enable_indexonlyscan    = false;
    enable_bitmapscan       = false;
}

static void         pg_plan_fuzz_force_indexonlyscan () 
{
    pg_plan_fuzz_set_config_option("enable_seqscan", "off");
    pg_plan_fuzz_set_config_option("enable_indexscan", "off");
    pg_plan_fuzz_set_config_option("enable_indexonlyscan", "on");
    pg_plan_fuzz_set_config_option("enable_bitmapscan", "off");

    enable_seqscan          = false;
    enable_indexscan        = false;
    enable_indexonlyscan    = true;
    enable_bitmapscan       = false;
}

static void         pg_plan_fuzz_force_bitmapscan    () 
{
    pg_plan_fuzz_set_config_option("enable_seqscan", "off");
    pg_plan_fuzz_set_config_option("enable_indexscan", "off");
    pg_plan_fuzz_set_config_option("enable_indexonlyscan", "off");
    pg_plan_fuzz_set_config_option("enable_bitmapscan", "on");

    enable_seqscan          = false;
    enable_indexscan        = false;
    enable_indexonlyscan    = false;
    enable_bitmapscan       = true;
}

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
        if (force_hashjoin || force_mergejoin || force_nestloop)
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

    foreach (lc, current_join_pathlist) 
    {
        path = (Path*)lfirst(lc);

        if (join_recourse_level_ == join_recourse_level) 
        {
            if (path->pathtype == type)     path->total_cost = YES;
            else                            path->total_cost = NO;
        }
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
            return;
        }
        default: { return; }
    }
}

static void pg_plan_fuzz_join_transformator () 
{
    if (join_recourse_level_ == join_recourse_level) 
    { 
        pg_plan_fuzz_join_operator();
    }
}

static void pg_plan_fuzz_force_mergejoin () 
{
    pg_plan_fuzz_set_config_option("enable_mergejoin", "on");
    pg_plan_fuzz_set_config_option("enable_hashjoin", "off");
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
    pg_plan_fuzz_set_config_option("enable_hashjoin",
         current_enable_hashjoin ? "on" : "off");
    pg_plan_fuzz_set_config_option("enable_mergejoin", 
        current_enable_mergejoin ? "on" : "off");
    pg_plan_fuzz_set_config_option("enable_nestloop", 
        current_enable_nestloop ? "on" : "off");
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
    scan_recourse_level_    = 1;
    relations_counter       = 0;
    multirelation           = false;
    current_joinrelids      = NULL;
    list_of_joinrelids      = NIL;
    *is_system              = false;
    pg_plan_fuzz_clear_join_enables();
    pg_plan_fuzz_clear_scan_enables();
}

static void pg_plan_fuzz_check_system_table (Oid relationObjectOid) 
{
    Relation    rel;
        
    if (OidIsValid(relationObjectOid)) 
    {
        rel = relation_open(relationObjectOid, NoLock);
        *is_system = IsSystemRelation(rel);
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
            DEBUG1,
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
    current_enable_hashjoin = enable_hashjoin;
    current_enable_mergejoin = enable_mergejoin;
    current_enable_nestloop = enable_nestloop;

    current_enable_seqscan          = enable_seqscan;
    current_enable_indexscan        = enable_indexscan;
    current_enable_indexonlyscan    = enable_indexonlyscan;

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

    if (logging_mode) 
    { 
        elog (INFO, "-----------------------------------------------------------------------------");
        elog (INFO, "                         LOGGING INFORMATION START");
        elog (INFO, "-----------------------------------------------------------------------------");
    }

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

    if (logging_mode)
    {
        elog (INFO, "-----------------------------------------------------------------------------");
        elog (INFO, "                         LOGGING INFORMATION END");
        elog (INFO, "-----------------------------------------------------------------------------");
    }

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

    if (*is_system) 
    { 
        if (prev_set_rel_pathlist)  prev_set_rel_pathlist(
            root,
            rel,
            rti,
            rte
        );
        return; 
    }
    
    
    if (scan_recourse_level_ == scan_recourse_level)
        pg_plan_fuzz_scan_operator();

    if (logging_mode) 
    {
        elog (INFO, "Enable SeqScan:\t\t\t%d", enable_seqscan);
        elog (INFO, "Enable IndexScan:\t\t%d", enable_indexscan);
        elog (INFO, "Enable IndexOnlyScan:\t\t%d", enable_indexonlyscan);
        elog (INFO, "Enable BitmapScan:\t\t%d", enable_bitmapscan);
        elog (INFO, "\n");
    }
    
    if (prev_set_rel_pathlist)  prev_set_rel_pathlist(
        root,
        rel,
        rti,
        rte
    );

    current_scan_pathlist = rel->pathlist;

    if (scan_recourse_level_ == scan_recourse_level)
        pg_plan_fuzz_scan_coster();

    scan_recourse_level_++;

    pg_plan_fuzz_clear_scan_enables();
}

static void pg_plan_fuzz_relation_info_hook (
    PlannerInfo     *root,
    Oid             relationObjectId,
    bool            inhparent, 
    RelOptInfo      *rel
) 
{
    pg_plan_fuzz_check_system_table(relationObjectId);  
    
    if (*is_system) 
    {
        if (prev_get_relation_info_hook)    prev_get_relation_info_hook(
            root,
            relationObjectId,
            inhparent,
            rel
        );
        return;
    }

    if (prev_get_relation_info_hook)    prev_get_relation_info_hook(
        root,
        relationObjectId,
        inhparent,
        rel
    );

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
    save_nestlevel = NewGUCNestLevel();
    if (!*is_system)
    {
        pg_plan_fuzz_join_set_relids(
            outerrel->relids,
            innerrel->relids
        );

        pg_plan_fuzz_join_transformator();
    }
    
    add_paths_to_joinrel(
        root, 
        joinrel, 
        outerrel, 
        innerrel, 
        jointype, 
        sjinfo, 
        restrictlist
    );

    if (!*is_system)
    {
        current_join_pathlist = joinrel->pathlist;

        if (logging_mode) 
        {

            ListCell *lc;
            Path     *path;

            foreach (lc, joinrel->pathlist) 
            {   
                path = (Path*)lfirst(lc);
                elog (
                    INFO, 
                    "Path type:\t\t\t%s",
                    pg_plan_fuzz_node_type_to_str(path->pathtype)
                );
            }
        }

        pg_plan_fuzz_join_coster();
    }
    
    AtEOXact_GUC(false, save_nestlevel);
}

void _PG_init (void) 
{
    elog(INFO, "Initialized pg_plan_fuzz extension");

    pg_plan_fuzz_init_scan_enables();
    pg_plan_fuzz_init();

    DefineCustomBoolVariable(
        "pg_plan_fuzz.logging_mode", 
        "Turn on | off logs",
        NULL, 
        &logging_mode,
        false,
        PGC_USERSET,
        0,
        NULL,
        NULL,
        NULL
    );

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

    DefineCustomBoolVariable(
        "pg_plan_fuzz.force_seqscan", 
        "Force planner/optimizer to use SeqScan in plan",
        NULL, 
        &force_seqscan,
        false,
        PGC_USERSET,
        0,
        NULL,
        NULL,
        NULL
    );

    DefineCustomBoolVariable(
        "pg_plan_fuzz.force_indexscan", 
        "Force planner/optimizer to use IndexScan in plan",
        NULL, 
        &force_indexscan,
        false,
        PGC_USERSET,
        0,
        NULL,
        NULL,
        NULL
    );

    DefineCustomBoolVariable(
        "pg_plan_fuzz.force_indexonlyscan", 
        "Force planner/optimizer to use IndexOnlyScan in plan",
        NULL, 
        &force_indexonlyscan,
        false,
        PGC_USERSET,
        0,
        NULL,
        NULL,
        NULL
    );

        DefineCustomBoolVariable(
        "pg_plan_fuzz.force_bitmapscan", 
        "Force planner/optimizer to use BitmapScan in plan",
        NULL, 
        &force_bitmapscan,
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
        NULL
    );

    DefineCustomIntVariable(
        "pg_plan_fuzz.scan_recourse_level",
        "Current query scan_recourse_level",
        NULL,
        &scan_recourse_level,
        0,
        0,
        MAX_JOIN_RECOURSE_LEVEL,
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

#define make_join_rel pg_plan_fuzz_make_join_rel
#define add_paths_to_joinrel add_paths_to_joinrel_wrapper
#include "core.c"
#include "make_join_rel.c"
#undef make_join_rel
#undef add_paths_to_joinrel

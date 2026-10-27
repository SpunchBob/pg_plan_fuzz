REGRESS_OPTS 	= --dbname=test
REGRESS 		= 	pg_plan_fuzz 			\
					hashjoin_to_nestloop	\
					hashjoin_to_mergejoin
PG_CONFIG 		= 	pg_config 
PGXS 			:= 	$(shell $(PG_CONFIG) --pgxs)

include $(PGXS)
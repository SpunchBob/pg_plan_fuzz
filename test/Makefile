REGRESS_OPTS 	= --dbname=test
REGRESS 		= 	pg_plan_fuzz 			\
					hashjoin_to_nestloop	\
					hashjoin_to_mergejoin	\
					mergejoin_to_hashjoin	\
					mergejoin_to_nestloop	\
					nestloop_to_mergejoin	\
					nestloop_to_hashjoin 	\
					indexscan_to_indexonlyscan \
					seqscan_to_indexscan \
					seqscan_to_indexonlyscan \
					indexscan_to_seqscan

PG_CONFIG 		= 	pg_config 
PGXS 			:= 	$(shell $(PG_CONFIG) --pgxs)

include $(PGXS)
MODULE_big 	= pg_plan_fuzz
OBJS 		= pg_plan_fuzz.o 
EXTENSION 	= pg_plan_fuzz 
DATA 		= pg_plan_fuzz--1.0.0.sql
CC 			= gcc
CXX 		= g++
PG_CONFIG 	?= pg_config
PGXS 		:= $(shell $(PG_CONFIG) --pgxs)

include $(PGXS)
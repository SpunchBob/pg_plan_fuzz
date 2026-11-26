# Postgres Plan Fuzzing Extension 
---

`pg_plan_fuzz` - is a Plan Fuzzing extension for **PostgreSQL 17**, which gives you ablitiy to generate different plans.
Postgres Plan Fuzzing Extension uses optimizer hook and wrapers of PostgreSQL 17 planner pipeline functions to change 
nodes of plan, that will be generating, by applying GUC's and costs rules for scan and join relations in query. It using 
a vector of bwords to specificate plan transformations. You can use it mechanicaly by changing vector, but u personally 
recomend you to use fuzzin-program for better experience.

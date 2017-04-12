#!/bin/bash

PGSQL_INSTALL="/home/hkucs/my_ubuntu/benchmark/postgresql"

dbname="dbtest"
client_num=16
table_num=16
record_num=40000
server_ip="10.22.1.160"

#drop_db=`$PGSQL_INSTALL/install/bin/dropdb dbtest -h $server_ip -p 7000`
#create_db=`$PGSQL_INSTALL/install/bin/createdb -O root dbtest -h $server_ip -p 7000`
#create_db=`$PGSQL_INSTALL/install/bin/createdb dbtest -h $server_ip -p 7000`
init_db=`$PGSQL_INSTALL/install/bin/pgbench -i -U root dbtest -h $server_ip -j 10 -c 20 -p 7000`

echo "$drop_db"
echo "$create_db"
echo "$init_db"

PGSQL='$PGSQL_INSTALL/install/bin/psql -d dbtest -h $server_ip -p 7000 -c'

for j in `seq 1 $table_num`;
do
        create_table=`$PGSQL_INSTALL/install/bin/psql -h $server_ip -p 7000 -d $dbname -c "CREATE TABLE employee${j} (id int, salary int, ADDRESS CHARACTER(256) DEFAULT 'Sandndddddddddddddddddddddddddddddddddddddddddddddddddddddexx');"`
        echo "$create_table"

        QUERY="INSERT INTO employee${j} VALUES (1, 5000)"

        for i in `seq 2 $record_num`;
        do
                n=$(($i % 1000))
                if [ "$n" -eq "0" ]
                then
                        eval $PGSQL "'$QUERY'"
                        QUERY="INSERT INTO employee${j} VALUES (1, 5000)"
                fi

                ADD_QUERY=", ($i, 5000)"
                QUERY=$QUERY$ADD_QUERY
        done
        #eval $PGSQL "'$QUERY'"
done

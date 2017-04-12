#!/bin/bash

PGSQL_INSTALL="/home/hkucs/my_ubuntu/benchmark/postgresql"

dbname="dbtest"
client_num=1
table_num=1
record_num=40000
server_ip="10.22.1.160"

per_responsible_table=$(($table_num / $client_num))
per_responsible_key=$(($record_num / $table_num))

rm cmd*
rm out*

check_size=16

for n in `seq 1 $client_num`;
do
        #echo "BEGIN;" >> cmd${n}
        start=$(($n * $per_responsible_table - $per_responsible_table + 1))
        end=$(($n * $per_responsible_table))
        echo "\setrandom delta 0 5000" >> cmd${n}
        for z in `seq $start $end`;
        do
                base_query="UPDATE employee${z} SET salary = 10 WHERE id % ${check_size} = 0;"
                #query_len=$(($record_num / $check_size - 1))
                #for y in `seq 1 $query_len`;
                #do
                        #base_num=$(($y * $check_size))
                        #range_num=$(($base_num + 1))
                        #add_query=" OR id BETWEEN $base_num AND $range_num"
                        #base_query=$base_query$add_query
                #done
                #end_point=";"
                #base_query=$base_query$end_point
                echo "$base_query" >> cmd${n}
        done
done

for k in `seq 1 $client_num`;
do
        launch_client=`$PGSQL_INSTALL/install/bin/pgbench dbtest -h $server_ip -j 1 -c 1 -p 7000 -T 100 -f /home/hkucs/Downloads/cmd${k} > out${k}.txt 2>&1 &`
        echo "$launch_client"
done

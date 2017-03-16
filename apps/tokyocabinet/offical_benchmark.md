- sudo apt-get install tokyotyrant

- tcrtest write [-port num] [-cnum num] [-tout num] [-nr] [-rnd] host rnum
-    Store records with keys of 8 bytes. They change as `00000001', `00000002'...
- tcrtest read [-port num] [-cnum num] [-tout num] [-mul num] [-rnd] host
-    Retrieve all records of the database above.
- tcrtest remove [-port num] [-cnum num] [-tout num] [-rnd] host
    Remove all records of the database above.
- tcrtest rcat [-port num] [-cnum num] [-tout num] [-shl num] [-dai|-dad] [-ext name] [-xlr|-xlg] host rnum
-    Store records with partway duplicated keys using concatenate mode.
- tcrtest misc [-port num] [-cnum num] [-tout num] host rnum
    Perform miscellaneous test of various operations.
- tcrtest wicked [-port num] [-cnum num] [-tout num] host rnum
    Perform updating operations of list and map selected at random.
- tcrtest table [-port num] [-cnum num] [-tout num] [-exp num] host rnum
-    Perform miscellaneous test of the table extension.

- Options feature the following.

-    -port num : specify the port number.
-    -cnum num : specify the number of connections.
-    -tout num : specify the timeout of each session in seconds.
-    -nr : use the function `tcrdbputnr' instead of `tcrdbput'.
-    -rnd : select keys at random.
-    -mul num : specify the number of records for the mget command.
-    -shl num : use `tcrdbputshl' and specify the width.
-    -dai : use `tcrdbaddint' instead of `tcrdbputcat'.
-    -dad : use `tcrdbadddouble' instead of `tcrdbputcat'.
-    -ext name : call a script language extension function.
-    -xlr : perform record locking.
-    -xlg : perform global locking.
-    -exp num : specify the lifetime of expiration test.


##Set Java Environment and Tomcat Evironment for TPCW##
###
JAVA_HOME
###
####
-If java is not installed,download java from http://www.oracle.com/technetwork/java/javase/downloads/jre8-downloads-2133155.html
-Then
-sudo vi /etc/profile
-add below to the tail fo profile
-export JAVA_HOME=[your java sdk directory(*/jdk-*)]
-PATH=$PATH:$JAVA_HOME

-If java had been installed,just ignore the steps above
####

###
CLASSPATH
###
####
-find h2-1.4.193.jar in h2/bin and servlet-api.jar in http://www.java2s.com/Code/Jar/s/Downloadservletapijar.htm

-export CLASSPATH=.:$JAVA_HOME/jre:/[your h2-1.4.193.jar directory/h2-1.4.193.jar]:[your servlet-api.jar directory/servlet-api.jar]
-PATH=$PATH:$CLASSPATH
####

###
CATALINA_HOME (FOR TOMCAT)
###
####
-export CATALINA_HOME=[your tomcat directory]
-PATH=$PATH:$CATALINA_HOME
-export PATH
####

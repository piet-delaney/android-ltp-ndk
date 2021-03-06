#!/bin/bash

# This script is a part of RPC & TI-RPC Test Suite
# (c) 2007 BULL S.A.S.
# Please refer to RPC & TI-RPC Test Suite documentation.
# More details at http://nfsv4.bullopensource.org/doc/rpc_testsuite.php

# This scripts launch everything needed to test RPC & TI-RPC
# Never try to launch alone, run "script_run.sh" instead
# Note : this script could be in more than one copy depending on what
#        tests series you want to run

# By C. LACABANNE - cyril.lacabanne@bull.net
# creation : 2007-06-05 revision : 2007-

# **********************
# *** INITIALISATION ***
# **********************

# simple test suite identification
TESTSUITENAME="RPC auth domain"
TESTSUITEVERS="0.1"
TESTSUITEAUTH="Cyril LACABANNE"
TESTSUITEDATE="2007-06-05"
TESTSUITECOMM=""

TESTSERVER_1_PATH="rpc_svc_1"
TESTSERVER_1_BIN="rpc_svc_1.bin"
TESTSERVER_1=$SERVERTSTPACKDIR/$TESTSERVER_1_PATH/$TESTSERVER_1_BIN

export TESTSERVER_1_PATH
export TESTSERVER_1_BIN
export TESTSERVER_1

# check if tests run locally or not
# if not, logs directory will be change to remote directory
if [ "$LOCALIP" != "$CLIENTIP" ]
then
	LOGDIR=/tmp/$LOGDIR
	if [ $VERBOSE -eq 1 ]
	then
		echo " - log dir changes to client log dir : "$LOGDIR # debug !
	fi
fi

# *****************
# *** PROCESSUS ***
# *****************

echo "*** Starting Test Suite : "$TESTSUITENAME" (v "$TESTSUITEVERS") ***"

#-- start TIRPC Server # 1 for that following tests series
if [ "$TESTWAY" = "onetomany" ]
then
	# run one server for one or more client
	$REMOTESHELL $SERVERUSER@$SERVERIP "$TESTSERVER_1 $PROGNUMBASE"&
else
	# launch as much server instances as client instances
	for ((a=0; a < TESTINSTANCE ; a++))
	do
		$REMOTESHELL $SERVERUSER@$SERVERIP "$TESTSERVER_1 `expr $PROGNUMBASE + $a`"&
	done
fi

#-- start another instance of TIRPC server for simple API call type test
$REMOTESHELL $SERVERUSER@$SERVERIP "$TESTSERVER_1 $PROGNUMNOSVC"&

# wait for server creation and initialization
sleep $SERVERTIMEOUT

### SCRIPT LIST HERE !!! ###
./$SCRIPTSDIR/rpc_auth_authnone_create.sh
./$SCRIPTSDIR/rpc_auth_authunix_create.sh
./$SCRIPTSDIR/rpc_auth_authunix_create_default.sh
./$SCRIPTSDIR/rpc_auth_auth_destroy.sh

#-- Cleanup
$REMOTESHELL $SERVERUSER@$SERVERIP "killall -9 "$TESTSERVER_1_BIN

# ***************
# *** RESULTS ***
# ***************

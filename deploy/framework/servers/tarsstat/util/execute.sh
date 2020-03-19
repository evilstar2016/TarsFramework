#!/bin/sh
# ulimit -a
bin="/usr/local/app/tars/tarsstat/bin/tarsstat"

OS=`uname`

if [[ "$OS" =~ "Darwin" ]]; then
    OS=1
else
    OS=0
fi

if [[ $OS == 1 ]]; then
PID=`ps -eopid,comm | grep "$bin"| grep "tarsstat" |  grep -v "grep" |awk '{print $1}'`
else
PID=`ps -eopid,cmd | grep "$bin"| grep "tarsstat" |  grep -v "grep" |awk '{print $1}'`
fi

#PID=`ps -eopid,cmd | grep "$bin"| grep "tarsstat" |  grep -v "grep" |awk '{print $1}'`

echo $PID

if [ "$PID" != "" ]; then
        kill -9 $PID
        echo "kill -9 $PID"
fi
ulimit -c unlimited
$bin  --config=/usr/local/app/tars/tarsstat/conf/tars.tarsstat.config.conf &

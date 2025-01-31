#!/bin/bash
set -e

CPODIR=/tmp/___cpo_integ_test
rm -rf ${CPODIR}
EPS=("tcp+k2rpc://0.0.0.0:10000" "tcp+k2rpc://0.0.0.0:10001" "tcp+k2rpc://0.0.0.0:10002" "tcp+k2rpc://0.0.0.0:10003" "tcp+k2rpc://0.0.0.0:10004")
NUMCORES=`nproc`
# core on which to run the TSO poller thread. Pick 4 if we have that many, or the highest-available otherwise
#TSO_POLLER_CORE=$(( 5 > $NUMCORES ? $NUMCORES-1 : 4 ))
# poll on random core
TSO_POLLER_CORE=-1
PERSISTENCE=tcp+k2rpc://0.0.0.0:12001
CPO=tcp+k2rpc://0.0.0.0:9000
TSO=tcp+k2rpc://0.0.0.0:13000
COMMON_ARGS="--reactor-backend=epoll --enable_tx_checksum true --thread-affinity false"
HTTP=tcp+k2rpc://0.0.0.0:20000


# start CPO
cpo_main ${COMMON_ARGS} -c1 --tcp_endpoints ${CPO} 9001 --data_dir ${CPODIR} --prometheus_port 63000 --assignment_timeout=1s --txn_heartbeat_deadline=1s --nodepool_endpoints ${EPS[@]} --tso_endpoints ${TSO} --tso_error_bound=100us --persistence_endpoints ${PERSISTENCE}&
cpo_child_pid=$!

# start nodepool
nodepool ${COMMON_ARGS} -c${#EPS[@]} --tcp_endpoints ${EPS[@]} --k23si_persistence_endpoint ${PERSISTENCE} --prometheus_port 63001 --memory=1G --partition_request_timeout=6s &
nodepool_child_pid=$!

# start persistence
persistence ${COMMON_ARGS} -c1 --tcp_endpoints ${PERSISTENCE} --prometheus_port 63002 &
persistence_child_pid=$!

# start tso
tso ${COMMON_ARGS} -c1 --tcp_endpoints ${TSO} --prometheus_port 63003 --tso.clock_poller_cpu=${TSO_POLLER_CORE} &
tso_child_pid=$!

sleep 1

# start http proxy
http_proxy ${COMMON_ARGS} -c1 --tcp_endpoints ${HTTP} --memory=1G --cpo ${CPO} --httpproxy_txn_timeout=100h --httpproxy_expiry_timer_interval=50ms --log_level=Info &
http_child_pid=$!

function finish {
  rv=$?
  # cleanup code
  rm -rf ${CPODIR}

  kill ${nodepool_child_pid}
  echo "Waiting for nodepool child pid: ${nodepool_child_pid}"
  wait ${nodepool_child_pid}

  kill ${cpo_child_pid}
  echo "Waiting for cpo child pid: ${cpo_child_pid}"
  wait ${cpo_child_pid}

  kill ${persistence_child_pid}
  echo "Waiting for persistence child pid: ${persistence_child_pid}"
  wait ${persistence_child_pid}

  kill ${tso_child_pid}
  echo "Waiting for tso child pid: ${tso_child_pid}"
  wait ${tso_child_pid}

  kill ${http_child_pid}
  echo "Waiting for http child pid: ${http_child_pid}"
  wait ${http_child_pid}

  echo ">>>> ${0} finished with code ${rv}"
}
sleep 1

echo ">>>> Done setting up cluster"

trap finish EXIT
trap ign INT

if [ -z "$1" ]
then
    echo ">>>> Press CTRL+C to exit..."
    wait ${http_child_pid}
else
    echo ">>>> Running" "${1}" "${@:2}"
    eval "${1}" "${@:2}"
fi


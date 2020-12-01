
DEFAULT_MESSAGE_SIZE=$((2 ** 7))
DEFAULT_THREADS=64
DEFAULT_SLEEP_SEC=180
SERVER_HOST=192.168.199.7

MAX_SOCKETS=3000
#MAX_CLIENTS=2000
MAX_CLIENTS=1000
ulimit -n $MAX_SOCKETS

if (( $# == 0 )) || (( $# > 4)); then
	echo "USAGE: $0 OUTPUT_FILEPATH [MESSAGE_SIZE [NUM_THREADS [TEST_DURATION_SEC]]]"
	echo "  OUTPUT_FILEPATH: filepath into which results are written"
	echo "  MESSAGE_SIZE: message size in bytes (default=$DEFAULT_MESSAGE_SIZE)"
	echo "  NUM_THREADS: number of threads to perform test (default=$DEFAULT_THREADS)"
	echo "  TEST_DURATION_SEC: duration of test in seconds (default=$DEFAULT_SLEEP_SEC)"
	exit -1
fi

cleanup() {
  for p in $pids; do
    kill -9 $p
  done
}


output_filename=$1
message_size=${2:-$DEFAULT_MESSAGE_SIZE}
num_threads=${3:-"$DEFAULT_THREADS"}
sleep_sec=${4:-"$DEFAULT_SLEEP_SEC"}

echo "Using $MAX_CLIENTS clients"
(./src/redis-benchmark -h $SERVER_HOST -n 1000000000 -c $MAX_CLIENTS --threads $num_threads -t set -d $message_size & echo $! >&3) 3>pid 2>&1 | tee "$output_filename" &

pids="$(<pid) $!"
echo "PIDs: $pids"

trap cleanup 2 3 9 15
sleep $sleep_sec
cleanup


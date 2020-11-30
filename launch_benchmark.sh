
DEFAULT_MESSAGE_SIZE=$((2 ** 7))
DEFAULT_THREADS=64
DEFAULT_SLEEP_SEC=180

MAX_SOCKETS=1000

if (( $# == 0 )) || (( $# > 4)); then
	echo "USAGE: $0 OUTPUT_FILEPATH [MESSAGE_SIZE [NUM_THREADS [TEST_DURATION_SEC]]]"
	echo "  OUTPUT_FILEPATH: filepath into which results are written"
	echo "  MESSAGE_SIZE: message size in bytes (default=$DEFAULT_MESSAGE_SIZE)"
	echo "  NUM_THREADS: number of threads to perform test (default=$DEFAULT_THREADS)"
	echo "  TEST_DURATION_SEC: duration of test in seconds (default=$DEFAULT_SLEEP_SEC)"
	exit -1
fi

output_filename=$1
message_size=${2:-$DEFAULT_MESSAGE_SIZE}
num_threads=${3:-"$DEFAULT_THREADS"}
sleep_sec=${4:-"$DEFAULT_SLEEP_SEC"}

num_clients=$(echo "$MAX_SOCKETS / $num_threads" | bc -l | cut -d '.' -f 1)
echo "Using $num_clients clients"
./src/redis-benchmark -n 1000000000 -c $num_clients --threads $num_threads -t set -d $((2 ** 7)) 2>&1 | tee "$output_filename" &
pid=$!

echo "PID: $pid"
sleep $sleep_sec
kill -9 $pid


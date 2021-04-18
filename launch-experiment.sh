#!/bin/bash

# recompile both redis directories
# 

BASELINE_SERVER="taskset -ac 0-15 /home/bryant/cs798/originalRedis/src/redis-server"
TEST_SERVER="taskset -ac 0-15 /home/bryant/cs798/redis/src/redis-server"
BENCHMARK="src/redis-benchmark"
CLI="./src/redis-cli"

REDIS_HOST_ADDR=192.168.249.107
REDIS_ADDR=192.168.199.7

REQUIRED_SERVER_FLAGS=""
REQUIRED_BENCHMARK_FLAGS="-h $REDIS_ADDR"
REQUIRED_CLI_FLAGS="-h $REDIS_ADDR"
TEST_SERVER_FLAGS=""
TEST_BENCHMARK_FLAGS="-c 500 --threads 64 --startup 30000 --duration 90000" #TODO fix number of clients!

KEY="key:__rand_int__"
SETUP_CONFIG_FILEPATH="/home/bryant/cs798/originalRedis/redis.conf" # default config with updated bind addresses
TEST_CONFIG_FILEPATH="/home/bryant/cs798/redis/redis.conf" # modified server config
SERVER_OUTPUT_DIR="/home/bryant/cs798/server-outputs"
BENCHMARK_OUTPUT_DIR="/home/bryant/cs798/benchmark-outputs"

launch_server() {
	local cmd="$1"
	local debug_str="$2"

	echo "Launching \"$cmd\""
	ssh "$REDIS_HOST_ADDR" "$cmd" &
	if [[ 0 != $? ]]; then
		echo "Failed to launch $debug_str server"
		return -1
	fi
	rval="$!"
	for i in $(seq 5 -1 0); do
		printf 'sleeping for %d\r' $i
		sleep 1
	done
	printf '                    \r'
	return 0
}

kill_server() {
	local server_ssh_pid=$1
	local debug_str="$2"

	# Kill redis server
	if ! ssh "$REDIS_HOST_ADDR" "killall redis-server" &> /dev/null; then
		echo "Failed to kill $debug_str server"
		exit -1
	fi
	echo "Killed server"

	wait $server_ssh_pid
	return
}

setup_test() {
	local server="$1"
	local test_op="$2"
	local size="$3"
	local debug_str="$4"

	local server_ssh_pid
	local rval

	# Launch server
	if ! launch_server "$server $SETUP_CONFIG_FILEPATH $REQUIRED_SERVER_FLAGS &> /dev/null" "$debug_str"; then
		return -1
	fi
	server_ssh_pid=$rval

	# Clear database
	if ! echo "FLUSHALL SYNC" | $CLI $REQUIRED_CLI_FLAGS &> /dev/null; then
		return -1
	fi

	if [[ "GET" == "$test_op" ]]; then
		# Set value of key getting retrieved
		if ! $BENCHMARK -t SET -c 1 -n 1 -d "$size" $REQUIRED_BENCHMARK_FLAGS &> /dev/null; then
			return -1
		fi

		# Validate that database entry was correctly set by restarting server and checking key
		# kill server
		if ! kill_server "$server_ssh_pid" "$debug_str"; then
			return -1
		fi

		# restart server
		if ! launch_server "$server $TEST_CONFIG_FILEPATH $REQUIRED_SERVER_FLAGS &> /dev/null" "$debug_str"; then
			return -1
		fi
		server_ssh_pid=$rval

		# check that size of value is that expected
		res="$(echo "GET $KEY" | $CLI $REQUIRED_CLI_FLAGS | wc -c)"
		if [[ 0 != $? ]] || [[ "$res" != "$(( $size + 1 ))" ]]; then
			echo "Something went wrong with setting up $debug_str server"
			return -1
		fi

		# kill server again
		if ! kill_server "$server_ssh_pid" "$debug_str"; then
			return -1
		fi
	elif [[ "SET" == "$test_op" ]]; then
		# Validate that database entry was cleared by restarting server and checking state of db
		# kill server
		if ! kill_server "$server_ssh_pid" "$debug_str"; then
			return -1
		fi

		# restart server
		if ! launch_server "$server $TEST_CONFIG_FILEPATH $REQUIRED_SERVER_FLAGS" "$debug_str"; then
			return -1
		fi
		server_ssh_pid=$rval

		# check that there are no entries
		rval="$(echo 'KEYS *' | $CLI $REQUIRED_CLI_FLAGS | wc -l)"
		if [[ 0 != $? ]] || [[ 0 != "$rval" ]]; then
			echo "Something went wrong with setting up $debug_str server"
			return -1
		fi

		# kill server again
		if ! kill_server "$server_ssh_pid" "$debug_str"; then
			return -1
		fi
	else
		echo "Test op $test_op not recognized for $debug_str server"
		return -1
	fi

	return 0
}

run_test() {
	local server="$1"
	local server_io_threads="$2"
	local test_op="$3"
	local size="$4"
	local debug_str="$5"
	local test_id="$debug_str"_thr-"$server_io_threads"_op-"$test_op"_sz-"$size"

	local server_cmd="$server $TEST_CONFIG_FILEPATH --io-threads $server_io_threads $REQUIRED_SERVER_FLAGS $TEST_SERVER_FLAGS"
	server_cmd="$server_cmd &> $SERVER_OUTPUT_DIR/server-$test_id"

	local benchmark_cmd="$BENCHMARK -t $test_op -d $size $REQUIRED_BENCHMARK_FLAGS $TEST_BENCHMARK_FLAGS"
	benchmark_cmd="$benchmark_cmd &> $BENCHMARK_OUTPUT_DIR/benchmark-$test_id"

	local server_ssh_pid

	# Sanity check
	ssh $REDIS_HOST_ADDR "killall redis-server" &> /dev/null

	# Launch server
	if ! launch_server "$server_cmd" "$test_id"; then
		return -1
	fi
	server_ssh_pid=$rval

	# Set value of key getting retrieved
	echo "Running \"$benchmark_cmd\""
	if ! eval "$benchmark_cmd"; then
		return -1
	fi

	# Kill server
	if ! kill_server "$server_ssh_pid" "$test_id"; then
		return -1
	fi
}

DEFAULT_QTYPE=lbsll

for test_op in GET; do
	for exp in $(seq 3 25); do
		for io_threads in 2 3; do
			ssh $REDIS_HOST_ADDR "killall redis-server" &> /dev/null
			size=$(( 2 ** $exp ))

			echo "Starting New Test:"
			echo "  IO Threads: $io_threads"
			echo "  Test Op: $test_op"
			echo "  Data Size: $size"
			echo

			# Setup servers
			echo ">> Starting Server Setup"
			if ! setup_test "$BASELINE_SERVER" "$test_op" "$size" "baseline" ||
			   ! setup_test "$TEST_SERVER $DEFAULT_QTYPE" "$test_op" "$size" "test"
			then
				exit -1
			fi
			echo "<< Server Setup Completed"
			echo

			# Run test using baseline server
			echo ">> Starting Tests"
			if ! run_test "$BASELINE_SERVER" "$io_threads" "$test_op" "$size" "baseline"; then
				exit -1
			fi

			for qtype in lbsll llk; do
				if ! run_test "$TEST_SERVER $qtype" "$io_threads" "$test_op" "$size" "test.$qtype"; then
					exit -1
				fi
			done
			echo "<< Tests Completed"
		done
	done
done


BIN=bin/nes
TESTDIR="test/roms/"
tests=($(find $TESTDIR -name "*.nes"))

function print_tests {
	i=0
	for f in "${tests[@]}"
	do
		printf " [%.3d] %s\n" $i "$f"
		i=$(( i + 1 ))
	done
	# print_files ${tests[@]}
}

echo "[ NES tests ]"
echo "there are ${#tests[@]} tests"
print_tests

i=0
while [[ 1 ]]
do
	echo -n ">> "
	read i

	case "$i" in
		"quit" | "q")
			break
			;;
	 	"list" | "l")
			print_tests
			;;
		*)
			echo "running ${tests[$i]}"
			$BIN "${tests[$i]}"
			;;
	esac
done

#!/bin/bash
# Benchmark script for Fast Blob Indexer variants
# Usage: ./challenge/benchmark.sh [N] [Q]
# Default: N=1000000, Q=100000

set -e

N=${1:-1000000}
Q=${2:-100000}
INPUT="/tmp/fbi_bench_input.txt"
RESULTS_DIR="/tmp/fbi_bench_results"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Fast Blob Indexer Benchmark ===${NC}"
echo "N = $N blobs, Q = $Q queries"
echo ""

# Check binaries
VARIANTS=()
for variant in basic ultra ultimate hyper; do
    if [ -f "bin/fbi_$variant" ]; then
        VARIANTS+=("$variant")
    else
        echo -e "${YELLOW}Warning: bin/fbi_$variant not found, skipping${NC}"
    fi
done

if [ ${#VARIANTS[@]} -eq 0 ]; then
    echo "Error: No binaries found in bin/"
    echo "Run: make && ./challenge/build_variants.sh"
    exit 1
fi

# Generate input
echo -e "${BLUE}Generating input...${NC}"
python3 challenge/gen_test.py $N $Q > "$INPUT"
INPUT_SIZE=$(wc -c < "$INPUT" | awk '{print $1}')
INPUT_SIZE_MB=$(awk "BEGIN {printf \"%.2f\", $INPUT_SIZE / 1024 / 1024}")
echo "Input: $INPUT_SIZE_MB MB"
echo ""

# Prepare results directory
mkdir -p "$RESULTS_DIR"

# Benchmark each variant
echo -e "${BLUE}Running benchmarks...${NC}"
declare -A TIMES
declare -A USER_TIMES
declare -A THROUGHPUTS

for variant in "${VARIANTS[@]}"; do
    OUTPUT="$RESULTS_DIR/out_$variant.txt"
    TIMELOG="$RESULTS_DIR/time_$variant.txt"
    
    echo -n "  fbi_$variant ... "
    
    # Run and capture timing (using bash time builtin)
    START=$(date +%s.%N)
    bin/fbi_$variant < "$INPUT" > "$OUTPUT"
    END=$(date +%s.%N)
    
    # Calculate elapsed time
    REAL=$(awk "BEGIN {printf \"%.3f\", $END - $START}")
    USER_TIMES[$variant]="0.000"  # Not available with simple timing
    TIMES[$variant]=$REAL
    USER_TIMES[$variant]=$USER
    
    # Calculate throughput (queries per second)
    THROUGHPUT=$(awk "BEGIN {printf \"%.0f\", $Q / $REAL}")
    THROUGHPUTS[$variant]=$THROUGHPUT
    
    echo -e "${GREEN}${REAL}s${NC} (${THROUGHPUT} q/s)"
done

echo ""

# Verify correctness
echo -e "${BLUE}Verifying correctness...${NC}"
BASELINE="${VARIANTS[0]}"
ALL_MATCH=true

for variant in "${VARIANTS[@]}"; do
    if [ "$variant" != "$BASELINE" ]; then
        if cmp -s "$RESULTS_DIR/out_$BASELINE.txt" "$RESULTS_DIR/out_$variant.txt"; then
            echo -e "  fbi_$variant vs fbi_$BASELINE: ${GREEN}OK${NC}"
        else
            echo -e "  fbi_$variant vs fbi_$BASELINE: ${YELLOW}MISMATCH${NC}"
            ALL_MATCH=false
        fi
    fi
done

if [ "$ALL_MATCH" = true ]; then
    echo -e "${GREEN}All outputs match!${NC}"
fi

echo ""

# Results table
echo -e "${BLUE}=== Results Summary ===${NC}"
printf "%-12s %10s %15s %10s\n" "Variant" "Time(s)" "Throughput" "Speedup"
printf "%-12s %10s %15s %10s\n" "--------" "-------" "-----------" "-------"

BASELINE_TIME=${TIMES[$BASELINE]}

for variant in "${VARIANTS[@]}"; do
    REAL=${TIMES[$variant]}
    THROUGHPUT=${THROUGHPUTS[$variant]}
    
    # Calculate speedup relative to baseline
    SPEEDUP=$(awk "BEGIN {printf \"%.2f\", $BASELINE_TIME / $REAL}")
    
    printf "%-12s %10.3f %12s q/s %9sx\n" \
        "fbi_$variant" "$REAL" "$THROUGHPUT" "$SPEEDUP"
done

echo ""

# Find fastest
FASTEST=""
FASTEST_TIME=999999
for variant in "${VARIANTS[@]}"; do
    TIME=${TIMES[$variant]}
    if (( $(awk "BEGIN {print ($TIME < $FASTEST_TIME)}") )); then
        FASTEST_TIME=$TIME
        FASTEST=$variant
    fi
done

echo -e "${GREEN}Fastest: fbi_$FASTEST (${FASTEST_TIME}s)${NC}"
echo ""

# Cleanup option
read -p "Keep test files in $RESULTS_DIR? [y/N] " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    rm -rf "$RESULTS_DIR" "$INPUT"
    echo "Cleaned up temporary files"
fi

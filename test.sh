set -e

echo "With zig cc:"

zig cc \
    -o ribboni \
    -O3 \
    main.c

time lua ./ack.lua
time ./ribboni

sudo perf stat -d -r 100 lua ./ack.lua
sudo perf stat -d -r 100 ./ribboni

# valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes ./ribboni

# rm cachegrind.out.*


# echo "With gcc:"

# gcc \
#     -O3 \
#     -o ribboni \
#     main.c

# time ./ribboni

# sudo perf stat -d -r 10 ./ribboni

# valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes ./ribboni

# rm cachegrind.out.*
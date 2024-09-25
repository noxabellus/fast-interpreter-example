set -e

echo "With zig cc:"

zig cc \
    -o interp \
    -O3 \
    main.c

time lua ./ack.lua
time ./interp

sudo perf stat -d -r 100 lua ./ack.lua
sudo perf stat -d -r 100 ./interp

# valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes ./interp

# rm cachegrind.out.*


# echo "With gcc:"

# gcc \
#     -O3 \
#     -o interp \
#     main.c

# time ./interp

# sudo perf stat -d -r 10 ./interp

# valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes ./interp

# rm cachegrind.out.*
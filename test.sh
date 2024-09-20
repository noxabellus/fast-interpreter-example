set -e

echo "With zig cc:"

zig cc \
    -O3 \
    -o ribboni \
    main.c

time ./ribboni

sudo perf stat -d -r 100 ./ribboni

valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes ./ribboni

rm cachegrind.out.*


# echo "With gcc:"

# gcc \
#     -O3 \
#     -o ribboni \
#     main.c

# time ./ribboni

# sudo perf stat -d -r 10 ./ribboni

# valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes ./ribboni

# rm cachegrind.out.*
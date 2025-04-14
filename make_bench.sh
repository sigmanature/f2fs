make O=$BASE/f2fs_bench ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig Image  -j8 &> $BASE/f2fs_bench/makebenchlog.txt

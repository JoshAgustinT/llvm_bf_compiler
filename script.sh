

            file=check.b
            file=benches/mandel.b

            # bench 
            # bottles X
            # deadcodetest
            # hanoi
            # hello
            # long X
            # loopremove ?
            # mandel
            # serptri
            # twinkle

make
./a.out benches/bench.b
llc bf.ll -o output.s
gcc output.s -o first_llvm 
time ./first_llvm
echo bench.b
echo

./a.out benches/bottles.b
llc bf.ll -o output.s
gcc output.s -o first_llvm 
time ./first_llvm
echo bottles.b
echo

./a.out benches/deadcodetest.b
llc bf.ll -o output.s
gcc output.s -o first_llvm 
time ./first_llvm
echo deadcodetest.b
echo

./a.out benches/hanoi.b
llc bf.ll -o output.s
gcc output.s -o first_llvm 
time ./first_llvm
echo hanoi.b
echo

./a.out benches/hello.b
llc bf.ll -o output.s
gcc output.s -o first_llvm 
time ./first_llvm
echo hello.b
echo

./a.out benches/long.b
llc bf.ll -o output.s
gcc output.s -o first_llvm 
time ./first_llvm
echo long.b
echo

./a.out benches/loopremove.b
llc bf.ll -o output.s
gcc output.s -o first_llvm 
time ./first_llvm
echo loopremove.b
echo

./a.out benches/mandel.b
llc bf.ll -o output.s
gcc output.s -o first_llvm 
time ./first_llvm
echo mandel.b
echo

./a.out benches/serptri.b
llc bf.ll -o output.s
gcc output.s -o first_llvm 
time ./first_llvm
echo serptri.b
echo

./a.out benches/twinkle.b
llc bf.ll -o output.s
gcc output.s -o first_llvm 
time ./first_llvm
echo twinkle.b
echo

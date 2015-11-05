#! /bin/sh
# useage
# ./createFileTest.sh begin end index
# ./createFileTest.sh 1 1000 0

content=""
i=$1;
max=$2;
size=$3
while [ $i -le $max ];
do
name=`printf "%d.log" $i`;
`dd if=/dev/urandom of=$name bs=$size count=1`
temp="$name"" ";
content=$content$temp;
i=$(($i+1));
done

indexfilename=`printf "index_%d" $4`;
`echo $content > $indexfilename`

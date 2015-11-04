#! /bin/sh
# useage
# ./createFileTest.sh begin end index
# ./createFileTest.sh 1 1000 0

content=""
i=$1;
max=$2;
while [ $i -le $max ];
do
name=`printf "%d.log" $i`;
`echo $i >> $name`;
`echo "sdfsdfsdf" >> $name`;
temp="$name"" ";
content=$content$temp;
i=$(($i+1));
done

indexfilename=`printf "index_%d" $3`;
`echo $content >> $indexfilename`

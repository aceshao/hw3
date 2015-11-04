#! /bin/sh
# useage
# ./createFileTest.sh 1 1000 client.log
indexfilename=$3
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

`echo $content > $indexfilename`



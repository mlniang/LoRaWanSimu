set terminal postscript eps color enh "Times-BoldItalic"
set output "states.eps"
set title "states over time"
set xlabel "time(Time::H)"
set ylabel "State"
set xrange [0:200]
					  set yrange [0:20]
					  set grid
					  set style line 1 linewidth 2
					  set style increment user
plot "-"  title "States" with lines
0.0416667 0
0.5625 0
1.08333 0
1.60417 0
2.125 0
2.64583 0
3.16667 0
3.6875 0
4.20833 0
4.72917 0
5.25 0
5.77083 0
6.29167 0
6.8125 0
7.33333 0
7.85417 0
8.375 10
8.89583 10
9.41667 10
9.9375 0
10.4583 0
10.9792 0
11.5 0
12.0208 0
12.5417 0
13.0625 0
13.5833 0
14.1042 5
14.625 5
15.1458 5
15.6667 5
16.1875 0
16.7083 0
17.2292 0
17.75 0
18.2708 5
18.7917 0
19.3125 0
19.8333 0
20.3542 0
20.875 0
21.3958 0
21.9167 0
22.4375 0
22.9583 0
23.4792 10
24 10
24.5208 10
e

#!/bin/bash
# tools/sheet.sh <test dir>: shots (shot*.bmp) -> 480 px PNGs and a 3-wide contact sheet sheet.png
cd "$1" || exit 1
n=0; in=(); for f in $(ls shot*.bmp 2>/dev/null | sort -V); do sips -s format png -Z 480 "$f" --out "s$n.png" >/dev/null 2>&1; in+=(-i "s$n.png"); n=$((n+1)); done
[ $n -eq 0 ] && { echo "no shots"; exit 1; }
while [ $((n % 3)) -ne 0 ]; do cp s0.png "s$n.png"; ffmpeg -loglevel error -y -f lavfi -i color=black:s=480x270 -frames:v 1 "s$n.png"; in+=(-i "s$n.png"); n=$((n+1)); done
f=""; r=""; for ((i=0;i<n;i++)); do f="$f[$i]scale=480:270[v$i];"; done
for ((i=0;i<n;i+=3)); do f="$f[v$i][v$((i+1))][v$((i+2))]hstack=3[r$i];"; r="$r[r$i]"; done
if [ $n -eq 3 ]; then F="${f%;}"; F="${F/hstack=3\[r0\]/hstack=3}"; else F="$f${r}vstack=$((n/3))"; fi
ffmpeg -loglevel error -y "${in[@]}" -filter_complex "$F" sheet.png && echo "$1/sheet.png ($n)"

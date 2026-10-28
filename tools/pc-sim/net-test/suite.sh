#!/bin/sh
# Scenarios radio / musique en 576p 50 Hz avec un faux Internet local (depuis la racine du depot) :
#   sh tools/pc-sim/net-test/suite.sh [scenario...]     (par defaut : tous ceux de net-test/sc)
# Resultats : tools/pc-sim/work/net-test/res/<scenario>-{log,fluidite}.txt. DRAWPROF=1 : profil du dessin.
# (Le simulateur fait confiance au certificat de test le temps de la suite, puis reprend la liste normale.)
R=$(pwd); T=$R/tools/pc-sim/net-test; W=$R/tools/pc-sim/work/net-test; L=$R/tools/pc-sim/work/libs
RUN=$R/tools/pc-sim/work/run; OUT=$W/res; mkdir -p $OUT
[ -d $L ] || sh $R/tools/pc-sim/fetch.sh
sh $T/setup.sh >/dev/null
python3 $R/tools/pem2h.py $W/cacert-test.pem $L/source/cacert.h >/dev/null && touch $R/source/main.c && make -s -C $R/tools/pc-sim -j"$(nproc)"
if [ ! -d $W/sd-ref ]; then
    rm -rf "$RUN/sd:"; sh $R/tools/pc-sim/demo-sd.sh $RUN >/dev/null
    printf 'wait 3s\nquit\n' > $W/init.txt; PCSIM_NET=off sh $R/tools/pc-sim/run.sh $W/init.txt >/dev/null 2>&1
    cp -r "$RUN/sd:" $W/sd-ref
fi
SC=${*:-$(ls $T/sc/*.txt)}
HOSTS=$(tr ',' '\n' < $T/hosts.txt | sed 's/$/=127.0.0.1/' | paste -sd, -)
python3 -I $W/net_srv.py > /dev/null 2>&1 &
PID=$!
sleep 1
for f in $SC; do
    n=$(basename $f .txt)
    rm -rf "$RUN/sd:"; cp -r $W/sd-ref "$RUN/sd:"
    G="$RUN/sd:/gc-mp3"
    { cat $T/stations.txt; grep -v '^#' $G/reglages/radio.txt; } > $W/radio.tmp && mv $W/radio.tmp $G/reglages/radio.txt
    for kv in "video=576p" "pochettes_auto=oui" "paroles_auto=oui" "radio_reprise=non" "reprise=non"; do
        k=${kv%%=*}; v=${kv#*=}
        if grep -q "^$k *=" $G/gc-mp3.txt; then sed -i "s|^$k *=.*|$k = $v|" $G/gc-mp3.txt; else echo "$k = $v" >> $G/gc-mp3.txt; fi
    done
    env ${DRAWPROF:+PCSIM_DRAWPROF=1} PCSIM_HOSTS="$HOSTS" PCSIM_PAL=1 PCSIM_COMPOSANTE=1 PCSIM_PERF=1 PCSIM_PERFLOG=1 PCSIM_SLOWMS=40 PCSIM_TIMEOUT=400 \
        sh $R/tools/pc-sim/run.sh $f > $OUT/$n-run.txt 2>&1
    cp $RUN/log.txt $OUT/$n-log.txt; cp $G/journaux/fluidite.txt $OUT/$n-fluidite.txt 2>/dev/null
    echo "=== $n"
    grep -E "\[pcsim\] (fluidite|son :)" $OUT/$n-log.txt
    grep -E "\[pcsim\] portion" $OUT/$n-log.txt | sed 's/\[pcsim\] portion /  /'
done
kill $PID 2>/dev/null
python3 $R/tools/pem2h.py $L/cacert.pem $L/source/cacert.h >/dev/null && touch $R/source/main.c

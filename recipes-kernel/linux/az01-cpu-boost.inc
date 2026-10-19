# Opt-in CPU boost: 1704 and 1800 MHz steps on plain RK3288 units.
#
#   AZ0X_CPU_BOOST = "1"   Beyond Rockchip's rating on plain RK3288.
#
# Ratings: Rockchip gives the Cortex-A17 cluster 1.6 GHz on plain RK3288 and
# 1.8 GHz on RK3288-C (datasheet Rev 2.4, p. 68). The -c trees, which u-boot
# loads only on RK3288-C units, already run to 1800 MHz; the plain trees stop
# at 1608 MHz. So there is nothing to add within rating, and this boost only
# touches plain trees that stop at 1608 MHz.
#
# Steps and voltages: exactly the -c trees' top two entries, 1704 MHz at
# 1.35 V and 1800 MHz at 1.40 V, which are also Rockchip's RK3288-CG values.
# APLL has both rates in its table. vdd_cpu is limited to 1.50 V on these
# boards, so both fit.
#
# Heat: stock trees have no CPU cooling at all, only the 113 C critical trip.
# With the boost, a passive trip on cpu-thermal at AZ0X_CPU_BOOST_LIMIT_MC
# steps the cluster back to 1608 MHz and no further, so the worst case is
# stock behaviour.
#
# Separate from the GPU boost (az01-gpu-boost.inc); either can be used alone.

AZ0X_CPU_BOOST ??= "0"
AZ0X_CPU_BOOST_STEPS ??= "1704000000:1350000 1800000000:1400000"
AZ0X_CPU_BOOST_LIMIT_MC ??= "85000"

do_compile[depends] += "${@'dtc-native:do_populate_sysroot' if d.getVar('AZ0X_CPU_BOOST') == '1' else ''}"
do_compile[vardeps] += "AZ0X_CPU_BOOST AZ0X_CPU_BOOST_STEPS AZ0X_CPU_BOOST_LIMIT_MC"
do_compile[postfuncs] += "${@'az0x_cpu_boost_dtbs' if d.getVar('AZ0X_CPU_BOOST') == '1' else ''}"

python az0x_cpu_boost_dtbs() {
    import os, re, subprocess

    def run(*args):
        return subprocess.run(args, check=True, capture_output=True, text=True).stdout

    def fdtget(dtb, *args):
        r = subprocess.run(('fdtget',) + (dtb,) + args, capture_output=True, text=True)
        return r.stdout.strip() if r.returncode == 0 else None

    steps = [tuple(int(v) for v in s.split(':')) for s in d.getVar('AZ0X_CPU_BOOST_STEPS').split()]
    limit = int(d.getVar('AZ0X_CPU_BOOST_LIMIT_MC'))
    dtsdir = os.path.join(d.getVar('B'), 'arch', d.getVar('ARCH'), 'boot', 'dts', 'rockchip')

    for board in (d.getVar('AZ0X_BOARD_DTBS') or '').split():
        dtb = os.path.join(dtsdir, board + '.dtb')
        if not os.path.exists(dtb):
            continue
        if 'rockchip,rk3288-c' in (fdtget(dtb, '/', 'compatible') or '').split():
            continue

        # The table cpu0 points at, found by phandle.
        cpu = '/cpus/cpu@500'
        table_ph = fdtget(dtb, '-t', 'u', cpu, 'operating-points-v2')
        cpu_ph = fdtget(dtb, '-t', 'u', cpu, 'phandle')
        if table_ph is None or cpu_ph is None or fdtget(dtb, cpu, '#cooling-cells') is None:
            continue
        table = None
        for n in (fdtget(dtb, '-l', '/') or '').split():
            if n.startswith('opp-table') and fdtget(dtb, '-t', 'u', '/' + n, 'phandle') == table_ph:
                table = '/' + n
        if table is None:
            continue

        # Only tables that stop at 1608 MHz, and not twice.
        opps = [int(o[4:]) for o in (fdtget(dtb, '-l', table) or '').split() if re.fullmatch(r'opp-\d+', o)]
        if max(opps, default=0) != 1608000000:
            continue

        dts = run('dtc', '-q', '-I', 'dtb', '-O', 'dts', dtb)
        used = [int(h, 16) for h in re.findall(r'phandle = <0x([0-9a-f]+)>;', dts)]
        trip = str(max(used, default=0) + 1)

        for hz, uv in steps:
            opp = '%s/opp-%d' % (table, hz)
            run('fdtput', '-c', dtb, opp)
            run('fdtput', '-t', 'u', dtb, opp, 'opp-hz', '0', str(hz))
            run('fdtput', '-t', 'u', dtb, opp, 'opp-microvolt', str(uv))

        t = '/thermal-zones/cpu-thermal/trips/cpu-boost-limit'
        run('fdtput', '-c', dtb, t)
        run('fdtput', '-t', 'u', dtb, t, 'temperature', str(limit))
        run('fdtput', '-t', 'u', dtb, t, 'hysteresis', '2000')
        run('fdtput', '-t', 's', dtb, t, 'type', 'passive')
        run('fdtput', '-t', 'u', dtb, t, 'phandle', trip)

        # cpufreq cooling state 0 is the top OPP; one state per added step
        # brings the cluster back down to 1608 MHz and no further.
        m = '/thermal-zones/cpu-thermal/cooling-maps/map-cpu-boost'
        run('fdtput', '-p', '-c', dtb, m)
        run('fdtput', '-t', 'u', dtb, m, 'trip', trip)
        run('fdtput', '-t', 'u', dtb, m, 'cooling-device', cpu_ph, '0', str(len(steps)))

        bb.note('AZ0X_CPU_BOOST: %s gets %s (plain RK3288, beyond rating)' % (
            board, ', '.join('%d MHz at %d uV' % (hz // 1000000, uv) for hz, uv in steps)))
}

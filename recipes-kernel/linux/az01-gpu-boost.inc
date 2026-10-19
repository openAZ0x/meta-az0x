# Opt-in GPU boost: a 600 MHz step (594 MHz from GPLL) on the boards whose
# device trees stop at 400 MHz.
#
#   AZ0X_GPU_BOOST = "1"        RK3288-C units only. Within Rockchip's rating.
#   AZ0X_GPU_BOOST_PLAIN = "1"  also plain RK3288 units. Beyond Rockchip's
#                               rating; needs AZ0X_GPU_BOOST = "1" as well.
#
# Ratings: Rockchip gives the Mali-T760 500 MHz on plain RK3288 and 600 MHz on
# RK3288-C (datasheet Rev 2.4, p. 68). The Prime players' trees stop at 400 MHz
# on both. u-boot loads the -c trees only on RK3288-C units, so the two cases
# map onto the plain and -c trees.
#
# Why plain silicon gets 600 rather than its rated 500: the GPU clock comes
# from CPLL (400 MHz) or GPLL (594 MHz); NPLL is the display's pixel clock and
# the USB PHY parent is marked unstable. A 500 MHz request runs at 400. The
# only faster step is 594 MHz, about 19% past the plain rating. inMusic ships
# exactly that, 600 MHz at 1.30 V, on plain RK3288 in the SC5000 and MPC trees,
# which is why it is offered at all, and why only as a separate opt-in.
#
# Voltage: 1.30 V, the value inMusic validated for 600 MHz on this SoC and
# SYR828 regulator. Rockchip's RK3288-CG table uses 1.25 V, but these boards
# already need 1.25 V at 400 MHz, 150 mV above Rockchip's 420 MHz entry. It
# stays under the regulator's 1.35 V maximum.
#
# Heat: stock runs these boards with no throttling. With the boost, a passive
# trip on gpu-thermal at AZ0X_GPU_BOOST_LIMIT_MC steps the GPU back to 400 MHz
# and no further, so the worst case is stock behaviour. The 113 C critical trip
# is untouched.
#
# The trees are edited after the kernel builds them; the kernel source and the
# trees outside the boost stay as they are.

AZ0X_GPU_BOOST ??= "0"
AZ0X_GPU_BOOST_PLAIN ??= "0"
AZ0X_GPU_BOOST_HZ ??= "600000000"
AZ0X_GPU_BOOST_UV ??= "1300000"
AZ0X_GPU_BOOST_LIMIT_MC ??= "85000"

do_compile[depends] += "${@'dtc-native:do_populate_sysroot' if d.getVar('AZ0X_GPU_BOOST') == '1' else ''}"
do_compile[vardeps] += "AZ0X_GPU_BOOST AZ0X_GPU_BOOST_PLAIN AZ0X_GPU_BOOST_HZ AZ0X_GPU_BOOST_UV AZ0X_GPU_BOOST_LIMIT_MC"
do_compile[postfuncs] += "${@'az0x_gpu_boost_dtbs' if d.getVar('AZ0X_GPU_BOOST') == '1' else ''}"

# make treats an existing .dtb as up to date, so a tree boosted by an earlier
# build would survive switching the boost off. Remove the board trees before
# every compile so they are always rebuilt from source.
do_compile[prefuncs] += "az0x_gpu_boost_fresh_dtbs"
python az0x_gpu_boost_fresh_dtbs() {
    import os
    dtsdir = os.path.join(d.getVar('B'), 'arch', d.getVar('ARCH'), 'boot', 'dts', 'rockchip')
    for board in (d.getVar('AZ0X_BOARD_DTBS') or '').split():
        dtb = os.path.join(dtsdir, board + '.dtb')
        if os.path.exists(dtb):
            os.remove(dtb)
}

python az0x_gpu_boost_dtbs() {
    import os, re, subprocess

    def run(*args):
        return subprocess.run(args, check=True, capture_output=True, text=True).stdout

    def fdtget(dtb, *args):
        r = subprocess.run(('fdtget',) + (dtb,) + args, capture_output=True, text=True)
        return r.stdout.strip() if r.returncode == 0 else None

    hz = int(d.getVar('AZ0X_GPU_BOOST_HZ'))
    uv = int(d.getVar('AZ0X_GPU_BOOST_UV'))
    limit = int(d.getVar('AZ0X_GPU_BOOST_LIMIT_MC'))
    plain = d.getVar('AZ0X_GPU_BOOST_PLAIN') == '1'
    dtsdir = os.path.join(d.getVar('B'), 'arch', d.getVar('ARCH'), 'boot', 'dts', 'rockchip')

    for board in (d.getVar('AZ0X_BOARD_DTBS') or '').split():
        dtb = os.path.join(dtsdir, board + '.dtb')
        if not os.path.exists(dtb):
            continue

        silicon_c = 'rockchip,rk3288-c' in (fdtget(dtb, '/', 'compatible') or '').split()
        if not (silicon_c or plain):
            continue

        # Only trees that stop at 400 MHz, and not twice.
        opps = (fdtget(dtb, '-l', '/opp-table-1') or '').split()
        if 'opp-400000000' not in opps:
            continue
        if any(int(o[4:]) > 400000000 for o in opps if re.fullmatch(r'opp-\d+', o)):
            continue

        # New phandles go above the highest one the tree uses.
        dts = run('dtc', '-q', '-I', 'dtb', '-O', 'dts', dtb)
        used = [int(h, 16) for h in re.findall(r'phandle = <0x([0-9a-f]+)>;', dts)]
        nxt = max(used, default=0)

        gpu = fdtget(dtb, '-t', 'u', '/gpu@ffa30000', 'phandle')
        if gpu is None:
            nxt += 1
            gpu = str(nxt)
            run('fdtput', '-t', 'u', dtb, '/gpu@ffa30000', 'phandle', gpu)
        nxt += 1
        trip = str(nxt)

        opp = '/opp-table-1/opp-%d' % hz
        run('fdtput', '-c', dtb, opp)
        run('fdtput', '-t', 'u', dtb, opp, 'opp-hz', '0', str(hz))
        run('fdtput', '-t', 'u', dtb, opp, 'opp-microvolt', str(uv))

        t = '/thermal-zones/gpu-thermal/trips/gpu-boost-limit'
        run('fdtput', '-c', dtb, t)
        run('fdtput', '-t', 'u', dtb, t, 'temperature', str(limit))
        run('fdtput', '-t', 'u', dtb, t, 'hysteresis', '2000')
        run('fdtput', '-t', 's', dtb, t, 'type', 'passive')
        run('fdtput', '-t', 'u', dtb, t, 'phandle', trip)

        # Devfreq cooling state 0 is the top OPP and 1 the next one down, so
        # limits 0..1 let the governor take the GPU from 600 to 400 MHz only.
        m = '/thermal-zones/gpu-thermal/cooling-maps/map-gpu-boost'
        run('fdtput', '-p', '-c', dtb, m)
        run('fdtput', '-t', 'u', dtb, m, 'trip', trip)
        run('fdtput', '-t', 'u', dtb, m, 'cooling-device', gpu, '0', '1')

        bb.note('AZ0X_GPU_BOOST: %s gets %d Hz at %d uV (%s)' % (
            board, hz, uv, 'RK3288-C' if silicon_c else 'plain RK3288, beyond rating'))
}

(Created by G-code exporter)
(Fri Nov  2 00:03:08 2012)
(Units: mm)
(Board size: 50.80 x 25.40 mm)
(Accuracy 600 dpi)
(Tool diameter: 0.000000 mm)
#100=2.000000  (safe Z)
#101=-0.050000  (cutting depth)
#102=25.000000  (plunge feedrate)
#103=50.000000  (feedrate)
(with predrilling)
(---------------------------------)
G17 G21 G90 G64 P0.003 M3 S3000 M7
G0 Z#100
(polygon 1)
G0 X27.770667 Y13.462000    (start point)
G1 Z#101 F#102
F#103
G1 X27.516667 Y13.335000
G1 X27.347333 Y13.208000
G1 X7.408333 Y13.165667
G1 X7.112000 Y12.869333
G1 X7.112000 Y12.488333
G1 X7.408333 Y12.192000
G1 X27.347333 Y12.149667
G1 X27.643667 Y11.938000
G1 X27.940000 Y11.895667
G1 X28.278667 Y11.980333
G1 X28.532667 Y12.192000
G1 X28.702000 Y12.530667
G1 X28.702000 Y12.827000
G1 X28.532667 Y13.165667
G1 X28.321000 Y13.335000
G1 X28.024667 Y13.462000
G1 X27.770667 Y13.462000
G0 Z#100
(polygon end, distance 44.84)
(predrilling)
F#102
G81 X27.940000 Y12.700000 Z#101 R#100
(1 predrills)
(milling distance 44.84mm = 1.77in)
M5 M9 M2
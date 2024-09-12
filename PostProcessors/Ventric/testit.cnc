(File created: Saturday April 20 2024 - 11:33 AM)
(for Carvera from Vectric)

( MANUAL NC COMMANDS)

( G0A90              -Rapids the a axis to degree position eg. 90)
( G53 G0 Z -2.       -Go to a safe z height ie. same height as the clearance position)
( G0A90 G53 G0 Z -2. -Moves the z axis up to its clearance position then moves the a axis)
( G92.4 A0 S0        -Shrinks the a axis with offset 0, so A365 will turn into A5)
( M5                 -Turns the spindle off)
( G28                -Goes to Carvea clearance position)
( G4 P4.             -Pause for X Seconds eg 4)
( M370               -Clears the auto level data from the machine)
( M220 S80           -Sets the feed override to a given percent eg.80. Useful for vetting a new program )
( M220 S100          -Resets the spindle override value to 100%)
( M7                 -Turns on the compressed Air)
( M9                 -Turns off the compressed Air)
( M331               -Turns on auto vacumn)
( M332               -Turns off auto vacumn)
( M801 S100          -Turns on vacuum)
( M802               -Turns off vacuum)
( M821               -Turns on the light)
( M822               -Turns off the light)
(M5 G28 M27 M5 M600  -Optonal Stop)
 
(Home positions to be used, as defined by the vCarve Material Setup Dialog: )
( XHome: X0.000, YHome: Y0.000, ZHome Z20.320)

(Material Setup)
(X Min = 0.000   Y Min = 0.000  Z Min = -38.100)
(X Max = 88.900   Y Max = 304.800  Z Max = 0.000)
(X Length = 88.900)
(Y Length = 304.800)
(Z Length = 38.100)


( 6 = End Mill - Diameter 0.25 inches - Collet .25 )
( 2 = V-Bit -  45.0 deg - Diameter 0.59375 inches - Collet .25)
( 6 = End Mill - Diameter 0.25 inches - Collet .25)
( 8 = End Mill - Diameter 0.25 inches - Collet .25 )
( 4 = End Mill - Diameter 0.25 inches - Collet .25 )
( 5 = End Mill - Diameter 0.125 inches - Collet .125)
( 2 = V-Bit -  45.0 deg - Diameter 0.59375 inches - Collet .25)
 
( Profile 1)
( Profile 2)
( Profile 3)
( Profile 5)
( Profile 5 [1])
( Profile 4)
( Profile 4 [1])
( Profile 2 [1])

G90G54
G17
G21

( Profile 1)
( Spec Tools Carbide )
( End Mill - Diameter 0.25 inches - Collet .25  )
 T6M6 
G0Z20.320
G0X0.000Y0.000
S16000M3
G0X31.750Y267.370
G0Z5.080
G1Z-2.540F508.0
G2X57.820Y241.300I0.000J-26.070F1270.0
G2X31.750Y215.230I-26.070J0.000
G2X5.680Y241.300I0.000J26.070
G2X31.750Y267.370I26.070J0.000
G0Z5.080
G0Z20.320
M5


( Profile 2)
( V-Bit -  45.0 deg - Diameter 0.59375 inches - Collet .25 )
( MiKerE 45 degree V-Grove .. 19/32 inch )
( Manual Tool Change To #2 )
( Setup for tool change )
 T-1 M6  G28 
( Paused. Prepare to add new tool to collet. Pressing play will close collet )
 M27 
 M600 
 M490.1 ( Close Collet )
( Paused. Pressing play will calibrate the tool length and continue the program )
 M27 
 M600 
 M493.2T1 (Set tool number to 1 so TLO can be set) 
 M491 (Calibrate Tool Length) 
S16000M3

( Profile 2)

G0X31.750Y265.247
G0Z5.080
G1Z-2.540F508.0
G2X55.697Y241.300I0.000J-23.947F1270.0
G2X31.750Y217.353I-23.947J0.000
G2X7.803Y241.300I0.000J23.947
G2X31.750Y265.247I23.947J0.000
G0Z5.080
G0Z20.320
M5


( Profile 3)
( End Mill - Diameter 0.25 inches - Collet .25 )
( Spec Tools Carbide )
( Setup for manual tool removal )
 G28 
( Paused. Prepare to remove tool from collet. Pressing play will release collet )
 M27 
 M600 
( Remove Tool )
 M490.2 
( Paused. Pressing play will resume program. Carvera expects an empty collet after this point )
 M27 
 M600 
 M493.2 T-1 
 T6M6 
S16000M3

( Profile 3)

G0X31.750Y267.370
G0Z5.080
G1Z-2.540F508.0
G2X57.820Y241.300I0.000J-26.070F1270.0
G2X31.750Y215.230I-26.070J0.000
G2X5.680Y241.300I0.000J26.070
G2X31.750Y267.370I26.070J0.000
G0Z5.080
G0Z20.320
M5


( Profile 5)
( End Mill - Diameter 0.25 inches - Collet .25  )
( Spec Tools Carbide )
( Manual Tool Change To #8 )
( Setup for tool change )
 T-1 M6  G28 
( Paused. Prepare to add new tool to collet. Pressing play will close collet )
 M27 
 M600 
 M490.1 ( Close Collet )
( Paused. Pressing play will calibrate the tool length and continue the program )
 M27 
 M600 
 M493.2T1 (Set tool number to 1 so TLO can be set) 
 M491 (Calibrate Tool Length) 
S16000M3

( Profile 5)

G0X31.750Y267.370
G0Z5.080
G1Z-2.540F508.0
G2X57.820Y241.300I0.000J-26.070F1270.0
G2X31.750Y215.230I-26.070J0.000
G2X5.680Y241.300I0.000J26.070
G2X31.750Y267.370I26.070J0.000
G0Z5.080
G0Z20.320
M5


( Profile 5 [1])
( End Mill - Diameter 0.25 inches - Collet .25  )
( Spec Tools Carbide )
( Setup for manual tool removal )
 G28 
( Paused. Prepare to remove tool from collet. Pressing play will release collet )
 M27 
 M600 
( Remove Tool )
 M490.2 
( Paused. Pressing play will resume program. Carvera expects an empty collet after this point )
 M27 
 M600 
 M493.2 T-1 
 T4M6 
S16000M3

( Profile 5 [1])

G0X31.750Y267.370
G0Z5.080
G1Z-2.540F508.0
G2X57.820Y241.300I0.000J-26.070F1270.0
G2X31.750Y215.230I-26.070J0.000
G2X5.680Y241.300I0.000J26.070
G2X31.750Y267.370I26.070J0.000
G0Z5.080
G0Z20.320
M5


( Profile 4)
( End Mill - Diameter 0.125 inches - Collet .125 )
( Genmitsu Nano Blue Coat )
( Setup for collet change )
 G28 
 T-1 M6 
( Paused. Prepare to remove collet. Pressing play will continue the program )
 M27 
 M600 
 M490.2 ( Change Collet )
 T5M6 
S16000M3

( Profile 4)

G0X31.750Y265.783
G0Z5.080
G1Z-1.270F508.0
G2X56.233Y241.300I0.000J-24.483F1270.0
G2X31.750Y216.817I-24.483J0.000
G2X7.267Y241.300I0.000J24.483
G2X31.750Y265.783I24.483J0.000
G1Z-2.540F508.0
G2X56.233Y241.300I0.000J-24.483F1270.0
G2X31.750Y216.817I-24.483J0.000
G2X7.267Y241.300I0.000J24.483
G2X31.750Y265.783I24.483J0.000
G0Z5.080

( Profile 4 [1])

G0X31.750Y265.783Z5.080
G1Z-1.270F508.0
G2X56.233Y241.300I0.000J-24.483F1270.0
G2X31.750Y216.817I-24.483J0.000
G2X7.267Y241.300I0.000J24.483
G2X31.750Y265.783I24.483J0.000
G1Z-2.540F508.0
G2X56.233Y241.300I0.000J-24.483F1270.0
G2X31.750Y216.817I-24.483J0.000
G2X7.267Y241.300I0.000J24.483
G2X31.750Y265.783I24.483J0.000
G0Z5.080
G0Z20.320
M5


( Profile 2 [1])
( V-Bit -  45.0 deg - Diameter 0.59375 inches - Collet .25 )
( MiKerE 45 degree V-Grove .. 19/32 inch )
( Manual Tool Change To #2 )
( Setup for tool change )
 T-1 M6  G28 
( Paused. Prepare to add new tool to collet. Pressing play will close collet )
 M27 
 M600 
 M490.1 ( Close Collet )
( Paused. Pressing play will calibrate the tool length and continue the program )
 M27 
 M600 
 M493.2T1 (Set tool number to 1 so TLO can be set) 
 M491 (Calibrate Tool Length) 
S16000M3

( Profile 2 [1])

G0X31.750Y265.247
G0Z5.080
G1Z-2.540F508.0
G2X55.697Y241.300I0.000J-23.947F1270.0
G2X31.750Y217.353I-23.947J0.000
G2X7.803Y241.300I0.000J23.947
G2X31.750Y265.247I23.947J0.000
G0Z5.080
M5
G0Z20.320
G0X0.000Y0.000
G28
 G28 
( Paused. Prepare to remove tool from collet. Pressing play will release collet )
 M27 
 M600 
( Remove Tool )
 M490.2 
( Paused. Pressing play will resume program. Carvera expects an empty collet after this point )
 M27 
 M600 
 M493.2 T-1 
M30

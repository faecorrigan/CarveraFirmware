+ =======================================
+ ---
+ Version 1
+   Josh     18/11/2022 Written
+   Clone of Fae Corrigan's Carvera Post Processor for Fusion -- Ed Wall  4/20/2024
+ =======================================


POST_NAME = "Carvera ATC_EW (mm) (*.cnc)"


FILE_EXTENSION = "cnc"


UNITS = "MM"



SCRIPT

require "strict"
  pp = require "ppVariables"
  
  --- global variables
  

  numberOfToolSlots = 9999
  previousToolChangeWasManual = false 
  colletChange = true

--     If colletChange is 'true' this post processor will prompt for a collet change whenever it detects
-- a bit shank change when transitioning between toolpaths. However, this requires that it is able 
-- to detect the shank size of both the current tool and its precursor. Unfortunately this property
-- is not provided by default in the Vectric tool database and I have choosen to pass this property in 
-- the tool name in the form: Collet x where x can be 3, 3.175, 4, 6, 6.35, .125 and .25 (The first five 
-- are, in effect, the possible collet size in millimeters. The latter two are, in effect, the possible collet size 
-- in inches). 'Collet x' can be placed anyplace in the tool name. I happen to use Custom Variables to
-- automatically create tool names of the form, for example, 'End Mill - Collet .25'.

--    If you set colletChange to 'false' then the post processor will not check for shank size
-- and will generate a functionally correct gcode file if all the bits you are using have the 
-- same shank size (that is, a collet change is nowhere necessary to execute the gcode file).

--    WARNING!!! If you do not pass shank size (or do so incorrectly) and colletChange is set 
-- to 'true' then the post processor will issue a warning and generate a functionally incomplete 
-- gcode file. I am not sure what would happen if you executed this gcode file with the Carvera, but I
-- suspect that it coud be very unpleasant.

  lineEnding = "\r\n"

 local prevtoolCollet = 0
 local currtoolCollet = 0



  -- DisplayMessageBox('Starting a Vectric vesion of Fae Post Processor')	

  --  = Initialise Variables =

  function main(script_path) 
    if pp.Init() == false then
        DisplayMessageBox('Failed to initialise ppVariables module!')
	   return false
    end       
      return true			
  end   


		-- Post-Processor Lua Subroutines.
    
  --- Output Subbroutines

  function formatComment(text)
    return "( " .. text .. " )" .. lineEnding
  end
     
  function writeComment(text)
    pp.PostP:OutputLine(formatComment(text), false)
  end
  
  function writeBlock(text)
      pp.PostP:OutputLine(" " .. text .. " ", false)
    end
    
  function newLine() 
     pp.PostP:OutputLine(lineEnding, false)
  end

  --- Condition Subrutines

  function checkForInName(text)

    local toolNameStr = tostring(pp.PostP:GetStringVariable('TOOLNAME'))
    local stridx = 1
    local endidx = 1

    stridx, endidx = string.find(toolNameStr,text,1,true)
    if stridx then
      return true
    else
      return false
    end
  end

  function checkForInPath(text)

    local toolNameStr = tostring(pp.PostP:GetStringVariable('TOOLPATH_NOTES'))
    local stridx = 1
    local endidx = 1

    stridx, endidx = string.find(toolNameStr,text,1,true)
    if stridx then
      return true
    else
      return false
    end
  end

function checkToolDia()

    local flag = false
    local tooldia = tonumber(tostring(pp.PostP:GetNumericVariable('TDIA')))

    if tooldia > 7 then
      flag = true
    end
    return flag
  end

  --- Extraction Subrouties

  function getCollet()
    
    local flag = 0
    
    if checkForInName("Collet .25") then
      flag = 1
    else
      if checkForInName("Collet .125") then
         flag = 4
      else  
         if checkForInName("Collet 6") then
            flag = 2
         else
            if checkForInName("Collet 6.35") then
               flag = 1
            else
               if checkForInName("Collet 4") then
                 flag = 3
               else   
                 if checkForInName("Collet 3.175") then
                    flag = 4
                 else
                    if checkForInName("Collet 3") then
                      flag = 5
                    end
                 end
               end
             end
         end
      end 
    end
    
    if ((flag == 0) and (colletChange)) then
      DisplayMessageBox('WARNING!! Unspecified shank size #' .. pp.T .. '  Invalid gcode file')
    end
    return flag
  end
 
          


   --- Tool Change Subroutines 

    function manualToolChangeRemove()
      writeBlock("G28")
      newLine()
      writeComment("Paused. Prepare to remove tool from collet. Pressing play will release collet")
      writeBlock("M27")
      newLine()
      writeBlock("M600")
      newLine()
      writeComment("Remove Tool")
      writeBlock("M490.2")
      newLine()
      writeComment("Paused. Pressing play will resume program. Carvera expects an empty collet after this point")
      writeBlock("M27")
      newLine()
      writeBlock("M600")
      newLine()
      writeBlock("M493.2 T-1") 
      newLine()
    end

    function manualToolChangeAdd()
      writeBlock("G28")
      newLine()
      writeComment("Paused. Prepare to add new tool to collet. Pressing play will close collet")
      writeBlock("M27")
      newLine()
      writeBlock("M600")
      newLine()
      writeBlock("M490.1")
      writeComment("Close Collet")
      writeComment("Paused. Pressing play will calibrate the tool length and continue the program")
      writeBlock("M27")
      newLine()
      writeBlock("M600")
      newLine()
      writeBlock("M493.2T1 (Set tool number to 1 so TLO can be set)")
      newLine()
      writeBlock("M491 (Calibrate Tool Length)")
       newLine()
    end

  function manualToolChange()

      writeComment("Manual Tool Change To #" .. pp.T)
      writeComment(pp.TOOLNAME)
      writeComment(pp.TOOL_NOTES)
      writeComment("Setup for tool change")
      manualToolChangeRemove()
      manualToolChangeAdd()
      previousToolChangeWasManual = true  
  end


  function onFirstSectionToolChange()
      
      local toolNumber = tonumber(tostring(pp.PostP:GetNumericVariable('T')))
      currtoolCollet = getCollet()
      prevtoolCollet = currtoolCollet
     
    if (toolNumber > numberOfToolSlots) then
        DisplayMessageBox('Tool number exceeds maximum value.')
        return false
    end
    
    if ((colletChange) and (currtoolCollet == 0 )) then
      return false
    end
    

    if (toolNumber > 6) or checkToolDia() then
      writeComment("Manual Tool Change To #" .. pp.T)
      writeComment(pp.TOOLNAME)
      writeComment(pp.TOOL_NOTES)
      writeComment("Setup for tool change")
      manualToolChangeRemove()
      manualToolChangeAdd()
      previousToolChangeWasManual = true  
    else
      writeComment(pp.TOOL_NOTES)
      writeComment(pp.TOOLNAME)
      writeBlock("T" .. pp.T .. "M6")
      newLine()
      previousToolChangeWasManual = false
    end
  end




  function onToolChange() 

  local toolNumber = tonumber(tostring(pp.PostP:GetNumericVariable('T')))
  local currtoolCollet = getCollet()

    if (toolNumber > numberOfToolSlots) then
      DisplayMessageBox('Tool number exceeds maximum value.')
      return false
    end

    if ((colletChange) and (currtoolCollet == 0 )) then
      return false
    end
    
    writeComment(pp.TOOLNAME)
    writeComment(pp.TOOL_NOTES)
    if (toolNumber > 6) or checkToolDia() then
      writeComment("Manual Tool Change To #" .. pp.T)
      writeComment("Setup for tool change")

      if (previousToolChangeWasManual)  then
        manualToolChangeRemove()
      else 
        writeBlock("T-1 M6")
      end
      manualToolChangeAdd()
      previousToolChangeWasManual = true      
    else
      if (previousToolChangeWasManual) then
          writeComment("Setup for manual tool removal")
          manualToolChangeRemove()
      else
         if (( currtoolCollet ~= prevtoolCollet ) and (colletChange)) then 
          writeComment("Setup for collet change")        
          writeBlock("G28")
          newLine()
          writeBlock("T-1 M6")
          newLine()
          writeComment("Paused. Prepare to remove collet. Pressing play will continue the program")
          writeBlock("M27")
          newLine()
          writeBlock("M600")
          newLine()
          writeBlock("M490.2")
          writeComment ("Change Collet")
          prevtoolCollet = currtoolCollet
         end 
      end 
        previousToolChangeWasManual = false
        writeBlock("T" .. pp.T .. "M6")
        newLine()
    end
  end

function onEndToolChange()

      if (previousToolChangeWasManual)  then
        manualToolChangeRemove()
        previousToolChangeWasManual = false
      end 
end

ENDSCRIPT
 
+------------------------------------------------
+    Line terminating characters
+------------------------------------------------

LINE_ENDING = "[13][10]"

+------------------------------------------------
+    Block numbering
+------------------------------------------------

LINE_NUMBER_START     = 0
LINE_NUMBER_INCREMENT = 10
LINE_NUMBER_MAXIMUM = 999999

+================================================
+
+    Formating for variables
+
+================================================

VAR LINE_NUMBER = [N|A|N|1.0]
VAR SPINDLE_SPEED = [S|A|S|1.0]
VAR FEED_RATE = [F|C|F|1.1]
VAR X_POSITION = [X|C|X|1.3]
VAR Y_POSITION = [Y|C|Y|1.3]
VAR Z_POSITION = [Z|C|Z|1.3]
VAR X_HOME_POSITION = [XH|A|X|1.3]
VAR Y_HOME_POSITION = [YH|A|Y|1.3]
VAR Z_HOME_POSITION = [ZH|A|Z|1.3]
VAR ARC_CENTRE_I_INC_POSITION = [I|A|I|1.3]
VAR ARC_CENTRE_J_INC_POSITION = [J|A|J|1.3]
VAR TOOL_DIAMETER = [TDIA|A||1.5]
 



+================================================
+
+    Block definitions for toolpath output
+
+================================================


+---------------------------------------------------
+  Commands output at the start of the file
+---------------------------------------------------

begin HEADER

"(File created: [DATE] - [TIME])"
"(for Carvera from Vectric)[10]"

"( MANUAL NC COMMANDS)"
""
"( G0A90              -Rapids the a axis to degree position eg. 90)"
"( G53 G0 Z -2.       -Go to a safe z height ie. same height as the clearance position)"
"( G0A90 G53 G0 Z -2. -Moves the z axis up to its clearance position then moves the a axis)"
"( G92.4 A0 S0        -Shrinks the a axis with offset 0, so A365 will turn into A5)"
"( M5                 -Turns the spindle off)"
"( G28                -Goes to Carvea clearance position)"
"( G4 P4.             -Pause for X Seconds eg 4)"
"( M370               -Clears the auto level data from the machine)"
"( M220 S80           -Sets the feed override to a given percent eg.80. Useful for vetting a new program )"
"( M220 S100          -Resets the spindle override value to 100%)"
"( M7                 -Turns on the compressed Air)"
"( M9                 -Turns off the compressed Air)"
"( M331               -Turns on auto vacumn)"
"( M332               -Turns off auto vacumn)"
"( M801 S100          -Turns on vacuum)"
"( M802               -Turns off vacuum)"
"( M821               -Turns on the light)"
"( M822               -Turns off the light)"
"(M5 G28 M27 M5 M600  -Optonal Stop)"

" "
"(Home positions to be used, as defined by the vCarve Material Setup Dialog: )"
"( XHome: [XH], YHome: [YH], ZHome [ZH])[10]"


"(Material Setup)"
"(X Min = [XMIN]   Y Min = [YMIN]  Z Min = [ZMIN])"
"(X Max = [XMAX]   Y Max = [YMAX]  Z Max = [ZMAX])"
"(X Length = [XLENGTH])"
"(Y Length = [YLENGTH])"
"(Z Length = [ZLENGTH])[10]"
""
"( [TOOLS_USED])"
" "
"( [TOOLPATHS_OUTPUT])"
""
"G90G54"
"G17"
"G21"
""
"( [TOOLPATH_NAME])"
"<!>onFirstSectionToolChange()"
"G0[ZH]"
"G0[XH][YH]"
"[S]M3"




+---------------------------------------------------
+  Commands output at toolchange
+---------------------------------------------------

begin TOOLCHANGE

"M5"
""
""
"( [TOOLPATH_NAME])"
"<!>onToolChange()"
"[S]M3"



+---------------------------------------------------
+  Commands output for new segment
+---------------------------------------------------

begin NEW_SEGMENT
""
"( [TOOLPATH_NAME])"
""

+---------------------------------------------------
+  Commands output for rapid moves
+---------------------------------------------------

begin RAPID_MOVE

"G0[X][Y][Z]"


+---------------------------------------------------
+  Commands output for the first feed rate move
+---------------------------------------------------

begin FIRST_FEED_MOVE

"G1[X][Y][Z][F]"


+---------------------------------------------------
+  Commands output for feed rate moves
+---------------------------------------------------

begin FEED_MOVE

"[X][Y][Z]"

+---------------------------------------------------
+  Commands output for the first clockwise arc move
+---------------------------------------------------

begin FIRST_CW_ARC_MOVE

"G2[X][Y][I][J][F]"


+---------------------------------------------------
+  Commands output for clockwise arc  move
+---------------------------------------------------

begin CW_ARC_MOVE

"G2[X][Y][I][J]"


+---------------------------------------------------
+  Commands output for the first counterclockwise arc move
+---------------------------------------------------

begin FIRST_CCW_ARC_MOVE

"G3[X][Y][I][J][F]"


+---------------------------------------------------
+  Commands output for counterclockwise arc  move
+---------------------------------------------------

begin CCW_ARC_MOVE

"G3[X][Y][I][J]"


+---------------------------------------------------
+  Commands output at the end of the file
+---------------------------------------------------

begin FOOTER

"M5"
"G0[ZH]"
"G0[XH][YH]"
"G28"
"<!>onEndToolChange()"
"M30"
// Lang.cpp - see Lang.h. The tables are UTF-8 text, one "English =
// translation" per line, as in a lang\<code>.txt file.
//
// The Hindi and Kannada words are a first draft: please have them checked
// by native speakers (and correct them here or in lang\hi.txt, lang\kn.txt).
#include "Lang.h"

#include "Document.h"  // TextFromUtf8

namespace {
const char* const kHindi = R"(
File = फ़ाइल
Edit = संपादन
View = दृश्य
Draw = बनाएँ
Modify = बदलें
Dimension = आयाम
Relate = संबंध
Annotate = टिप्पणी
Format = प्रारूप
Tools = उपकरण
Graphics = ग्राफ़िक्स
Help = सहायता
Search commands   Ctrl+K = कमांड खोजें   Ctrl+K
Choose a tool above (Line, Circle, Rectangle...) or type its name below (L, C, REC) and press Enter. Click an object to select it. = ऊपर से कोई उपकरण चुनें (रेखा, वृत्त, आयत...) या नीचे उसका नाम लिखें (L, C, REC) और Enter दबाएँ। किसी वस्तु को चुनने के लिए उस पर क्लिक करें।
# File
New = नया
Open (drawing or DXF)... = खोलें (ड्रॉइंग या DXF)...
Open recent = हाल में खोली गई
Save = सहेजें
Save as... = इस नाम से सहेजें...
Close drawing = ड्रॉइंग बंद करें
Print... = प्रिंट करें...
Export PDF... = PDF बनाएँ...
Export PNG picture... = PNG चित्र बनाएँ...
Export SVG drawing... = SVG ड्रॉइंग बनाएँ...
Export DXF (for AutoCAD and others)... = DXF बनाएँ (AutoCAD आदि के लिए)...
Import DXF into this drawing... = इस ड्रॉइंग में DXF लाएँ...
Drawing properties (title block)... = ड्रॉइंग की जानकारी (शीर्षक खंड)...
Version history... = पिछले संस्करण...
Recover drawings... = ड्रॉइंग वापस पाएँ...
Exam sheet (my details filled in)... = परीक्षा शीट (मेरा विवरण भरा हुआ)...
My details (name, USN)... = मेरा विवरण (नाम, USN)...
Exit = बाहर निकलें
# Edit
Undo = पूर्ववत करें
Redo = फिर से करें
Cut = काटें
Copy = कॉपी करें
Paste = चिपकाएँ
Select all = सब चुनें
Quick select... = जल्दी चुनें...
Select similar = मिलते-जुलते चुनें
Erase = मिटाएँ
Find and replace text... = पाठ खोजें और बदलें...
Group = समूह बनाएँ
Ungroup = समूह तोड़ें
Search commands... = कमांड खोजें...
# View
Zoom extents = पूरी ड्रॉइंग दिखाएँ
Zoom window = चुना भाग बड़ा करें
Zoom previous = पिछला दृश्य
Zoom in = बड़ा करें
Zoom out = छोटा करें
Pan = खिसकाकर देखें
Hide objects for now = वस्तुएँ अभी छिपाएँ
Isolate (show only these) = केवल इन्हें दिखाएँ
Show hidden objects again = छिपी वस्तुएँ फिर दिखाएँ
Replay the drawing step by step = ड्रॉइंग को चरण-दर-चरण फिर से देखें
Next drawing = अगली ड्रॉइंग
Previous drawing = पिछली ड्रॉइंग
Sheets = शीट
Theme = रंग-रूप
Language = भाषा
# Draw
Line = रेखा
Polyline = पॉलीलाइन
Circle = वृत्त
Arc = चाप
Rectangle = आयत
Polygon = बहुभुज
Ellipse = दीर्घवृत्त
Spline = वक्र (स्प्लाइन)
Point = बिंदु
Projector (construction line) = प्रक्षेपक (रचना रेखा)
# Modify
Move = खिसकाएँ
Rotate = घुमाएँ
Mirror = दर्पण प्रतिबिंब
Scale = आकार बदलें
Stretch = खींचें
Trim = छाँटें (ट्रिम)
Extend = बढ़ाएँ
Offset = समांतर प्रति (ऑफ़सेट)
Fillet = गोलाई (फ़िलेट)
Chamfer = किनारा काटें (चैम्फ़र)
Lengthen = लंबाई बदलें
Break = तोड़ें
Join = जोड़ें
Explode = अलग करें
Array = सरणी (ऐरे)
Align (two points onto two) = मिलाएँ (दो बिंदु दो पर)
Hidden line on/off = छिपी रेखा चालू/बंद
# Dimension
Smart dimension = स्मार्ट आयाम
Quick dimension (many at once) = त्वरित आयाम (एक साथ कई)
Linear = रैखिक
Aligned = संरेखित
Radius = त्रिज्या
Diameter = व्यास
Angular = कोणीय
Tolerance and fits (H7/g6)... = सहनशीलता और फ़िट (H7/g6)...
Isometric dimension = आइसोमेट्रिक आयाम
# Annotate and format
Text = पाठ
Edit text (or double-click it) = पाठ बदलें (या दो बार क्लिक करें)
Text style (font, width, slant)... = पाठ शैली (फ़ॉन्ट, चौड़ाई, झुकाव)...
Leader = संकेत रेखा (लीडर)
Hatch = छायांकन (हैच)
Layers... = परतें (लेयर)...
Properties panel = गुण पैनल
Match properties = गुण मिलाएँ
Units... = इकाइयाँ...
Dimension style... = आयाम शैली...
Measure distance = दूरी मापें
Measure area = क्षेत्रफल मापें
# Graphics (engineering drawing)
Isometric text = आइसोमेट्रिक पाठ
Isometric circle = आइसोमेट्रिक वृत्त
True length and angles of a line = रेखा की वास्तविक लंबाई और कोण
Projections of a point (quadrants) = बिंदु के प्रक्षेप (चतुर्थांश)
Traces of a line (HT, VT) = रेखा के ट्रेस (HT, VT)
Geometric constructions = ज्यामितीय रचनाएँ
Bisector of a line or an angle = रेखा या कोण का समद्विभाजक
Parallel through a point = बिंदु से समांतर रेखा
Perpendicular from a point = बिंदु से लंब
Tangents from a point to a circle = बिंदु से वृत्त पर स्पर्श रेखाएँ
Divide into equal parts = बराबर भागों में बाँटें
Measure off equal lengths = बराबर लंबाइयाँ नापें
Plain or diagonal scale (RF) = सादा या विकर्ण स्केल (RF)
Development of surfaces = सतहों का विकास
Transition piece (square to round) = संक्रमण खंड (वर्ग से गोल)
Intersection of solids = ठोसों का प्रतिच्छेदन
Perspective of a block = ब्लॉक का परिप्रेक्ष्य चित्र
Cutting plane line (A-A) = काट तल रेखा (A-A)
True shape of a section = काट का वास्तविक आकार
First-angle projection symbol = प्रथम कोण प्रक्षेप चिह्न
BIS (SP 46) drawing setup = BIS (SP 46) ड्रॉइंग व्यवस्था
Engineering curves = इंजीनियरिंग वक्र
Conic by eccentricity (ellipse, parabola, hyperbola) = उत्केंद्रता से शांकव (दीर्घवृत्त, परवलय, अतिपरवलय)
Cycloid = चक्रज (साइक्लॉइड)
Epicycloid = अधिचक्रज (एपिसाइक्लॉइड)
Hypocycloid = अंतश्चक्रज (हाइपोसाइक्लॉइड)
Involute of a circle or polygon = वृत्त या बहुभुज का इन्वोल्यूट
Archimedean spiral = आर्किमिडीज़ सर्पिल
Helix = कुंडलिनी (हेलिक्स)
Tangent and normal at a point = बिंदु पर स्पर्श रेखा और अभिलंब
Symbols (electrical, logic, flowchart, network, civil)... = प्रतीक (विद्युत, लॉजिक, फ़्लोचार्ट, नेटवर्क, सिविल)...
Wire / connector (stays joined) = तार / जोड़ (जुड़ा रहता है)
# Tools and help
Command aliases (my shortcuts)... = कमांड के छोटे नाम (मेरे शॉर्टकट)...
Commands... = सभी कमांड...
Mouse and keyboard tips = माउस और कीबोर्ड के सुझाव
Examples: solved problems with their steps... = उदाहरण: चरणों सहित हल किए गए प्रश्न...
About Feather CAD = Feather CAD के बारे में
)";

const char* const kKannada = R"(
File = ಕಡತ
Edit = ಸಂಪಾದನೆ
View = ನೋಟ
Draw = ರಚಿಸಿ
Modify = ಮಾರ್ಪಡಿಸಿ
Dimension = ಆಯಾಮ
Relate = ಸಂಬಂಧ
Annotate = ಟಿಪ್ಪಣಿ
Format = ಸ್ವರೂಪ
Tools = ಉಪಕರಣಗಳು
Graphics = ಗ್ರಾಫಿಕ್ಸ್
Help = ಸಹಾಯ
Search commands   Ctrl+K = ಆದೇಶ ಹುಡುಕಿ   Ctrl+K
Choose a tool above (Line, Circle, Rectangle...) or type its name below (L, C, REC) and press Enter. Click an object to select it. = ಮೇಲಿನಿಂದ ಒಂದು ಉಪಕರಣ ಆರಿಸಿ (ರೇಖೆ, ವೃತ್ತ, ಆಯತ...) ಅಥವಾ ಕೆಳಗೆ ಅದರ ಹೆಸರು ಟೈಪ್ ಮಾಡಿ (L, C, REC) ಮತ್ತು Enter ಒತ್ತಿ. ವಸ್ತುವನ್ನು ಆರಿಸಲು ಅದರ ಮೇಲೆ ಕ್ಲಿಕ್ ಮಾಡಿ.
# File
New = ಹೊಸದು
Open (drawing or DXF)... = ತೆರೆಯಿರಿ (ಚಿತ್ರ ಅಥವಾ DXF)...
Open recent = ಇತ್ತೀಚಿನವು
Save = ಉಳಿಸಿ
Save as... = ಬೇರೆ ಹೆಸರಿನಲ್ಲಿ ಉಳಿಸಿ...
Close drawing = ಚಿತ್ರ ಮುಚ್ಚಿ
Print... = ಮುದ್ರಿಸಿ...
Export PDF... = PDF ಆಗಿ ಉಳಿಸಿ...
Export PNG picture... = PNG ಚಿತ್ರವಾಗಿ ಉಳಿಸಿ...
Export SVG drawing... = SVG ಆಗಿ ಉಳಿಸಿ...
Export DXF (for AutoCAD and others)... = DXF ಆಗಿ ಉಳಿಸಿ (AutoCAD ಮುಂತಾದವುಗಳಿಗೆ)...
Import DXF into this drawing... = ಈ ಚಿತ್ರಕ್ಕೆ DXF ತನ್ನಿ...
Drawing properties (title block)... = ಚಿತ್ರದ ವಿವರಗಳು (ಶೀರ್ಷಿಕೆ ಪಟ್ಟಿ)...
Version history... = ಹಿಂದಿನ ಆವೃತ್ತಿಗಳು...
Recover drawings... = ಚಿತ್ರಗಳನ್ನು ಮರಳಿ ಪಡೆಯಿರಿ...
Exam sheet (my details filled in)... = ಪರೀಕ್ಷಾ ಹಾಳೆ (ನನ್ನ ವಿವರಗಳೊಂದಿಗೆ)...
My details (name, USN)... = ನನ್ನ ವಿವರಗಳು (ಹೆಸರು, USN)...
Exit = ನಿರ್ಗಮಿಸಿ
# Edit
Undo = ರದ್ದುಮಾಡಿ
Redo = ಮತ್ತೆ ಮಾಡಿ
Cut = ಕತ್ತರಿಸಿ
Copy = ನಕಲಿಸಿ
Paste = ಅಂಟಿಸಿ
Select all = ಎಲ್ಲವನ್ನೂ ಆರಿಸಿ
Quick select... = ತ್ವರಿತ ಆಯ್ಕೆ...
Select similar = ಹೋಲುವವನ್ನು ಆರಿಸಿ
Erase = ಅಳಿಸಿ
Find and replace text... = ಪಠ್ಯ ಹುಡುಕಿ ಮತ್ತು ಬದಲಿಸಿ...
Group = ಗುಂಪು ಮಾಡಿ
Ungroup = ಗುಂಪು ಬಿಡಿಸಿ
Search commands... = ಆದೇಶ ಹುಡುಕಿ...
# View
Zoom extents = ಪೂರ್ಣ ಚಿತ್ರ ತೋರಿಸಿ
Zoom window = ಆರಿಸಿದ ಭಾಗ ಹಿಗ್ಗಿಸಿ
Zoom previous = ಹಿಂದಿನ ನೋಟ
Zoom in = ಹಿಗ್ಗಿಸಿ
Zoom out = ಕುಗ್ಗಿಸಿ
Pan = ಸರಿಸಿ ನೋಡಿ
Hide objects for now = ವಸ್ತುಗಳನ್ನು ಈಗ ಮರೆಮಾಡಿ
Isolate (show only these) = ಇವನ್ನು ಮಾತ್ರ ತೋರಿಸಿ
Show hidden objects again = ಮರೆಮಾಡಿದವನ್ನು ಮತ್ತೆ ತೋರಿಸಿ
Replay the drawing step by step = ಚಿತ್ರವನ್ನು ಹಂತ ಹಂತವಾಗಿ ಮತ್ತೆ ನೋಡಿ
Next drawing = ಮುಂದಿನ ಚಿತ್ರ
Previous drawing = ಹಿಂದಿನ ಚಿತ್ರ
Sheets = ಹಾಳೆಗಳು
Theme = ಬಣ್ಣದ ಶೈಲಿ
Language = ಭಾಷೆ
# Draw
Line = ರೇಖೆ
Polyline = ಪಾಲಿಲೈನ್
Circle = ವೃತ್ತ
Arc = ಚಾಪ
Rectangle = ಆಯತ
Polygon = ಬಹುಭುಜಾಕೃತಿ
Ellipse = ದೀರ್ಘವೃತ್ತ
Spline = ವಕ್ರರೇಖೆ (ಸ್ಪ್ಲೈನ್)
Point = ಬಿಂದು
Projector (construction line) = ಪ್ರಕ್ಷೇಪಕ (ರಚನಾ ರೇಖೆ)
# Modify
Move = ಸ್ಥಳಾಂತರಿಸಿ
Rotate = ತಿರುಗಿಸಿ
Mirror = ಪ್ರತಿಬಿಂಬ
Scale = ಗಾತ್ರ ಬದಲಿಸಿ
Stretch = ಎಳೆದು ಹಿಗ್ಗಿಸಿ
Trim = ಕತ್ತರಿಸಿ (ಟ್ರಿಮ್)
Extend = ವಿಸ್ತರಿಸಿ
Offset = ಸಮಾಂತರ ನಕಲು (ಆಫ್‌ಸೆಟ್)
Fillet = ದುಂಡಾಗಿಸಿ (ಫಿಲೆಟ್)
Chamfer = ಅಂಚು ಕತ್ತರಿಸಿ (ಚಾಂಫರ್)
Lengthen = ಉದ್ದ ಬದಲಿಸಿ
Break = ಮುರಿಯಿರಿ
Join = ಜೋಡಿಸಿ
Explode = ಬಿಡಿಸಿ
Array = ಸರಣಿ (ಅರೇ)
Align (two points onto two) = ಹೊಂದಿಸಿ (ಎರಡು ಬಿಂದುಗಳನ್ನು ಎರಡರ ಮೇಲೆ)
Hidden line on/off = ಅಡಗಿದ ರೇಖೆ ಆನ್/ಆಫ್
# Dimension
Smart dimension = ಸ್ಮಾರ್ಟ್ ಆಯಾಮ
Quick dimension (many at once) = ತ್ವರಿತ ಆಯಾಮ (ಒಮ್ಮೆಗೆ ಹಲವು)
Linear = ರೇಖೀಯ
Aligned = ಓರೆಯಾದ (ಅಲೈನ್ಡ್)
Radius = ತ್ರಿಜ್ಯ
Diameter = ವ್ಯಾಸ
Angular = ಕೋನ
Tolerance and fits (H7/g6)... = ಸಹಿಷ್ಣುತೆ ಮತ್ತು ಫಿಟ್ (H7/g6)...
Isometric dimension = ಐಸೋಮೆಟ್ರಿಕ್ ಆಯಾಮ
# Annotate and format
Text = ಪಠ್ಯ
Edit text (or double-click it) = ಪಠ್ಯ ಬದಲಿಸಿ (ಅಥವಾ ಎರಡು ಬಾರಿ ಕ್ಲಿಕ್ ಮಾಡಿ)
Text style (font, width, slant)... = ಪಠ್ಯ ಶೈಲಿ (ಅಕ್ಷರ, ಅಗಲ, ಓರೆ)...
Leader = ಸೂಚಕ ರೇಖೆ (ಲೀಡರ್)
Hatch = ಛಾಯೆ (ಹ್ಯಾಚ್)
Layers... = ಪದರಗಳು (ಲೇಯರ್)...
Properties panel = ಗುಣಲಕ್ಷಣ ಫಲಕ
Match properties = ಗುಣಲಕ್ಷಣ ಹೊಂದಿಸಿ
Units... = ಮಾನಗಳು...
Dimension style... = ಆಯಾಮ ಶೈಲಿ...
Measure distance = ದೂರ ಅಳೆಯಿರಿ
Measure area = ವಿಸ್ತೀರ್ಣ ಅಳೆಯಿರಿ
# Graphics (engineering drawing)
Isometric text = ಐಸೋಮೆಟ್ರಿಕ್ ಪಠ್ಯ
Isometric circle = ಐಸೋಮೆಟ್ರಿಕ್ ವೃತ್ತ
True length and angles of a line = ರೇಖೆಯ ನಿಜವಾದ ಉದ್ದ ಮತ್ತು ಕೋನಗಳು
Projections of a point (quadrants) = ಬಿಂದುವಿನ ಪ್ರಕ್ಷೇಪಗಳು (ಚತುರ್ಥಕಗಳು)
Traces of a line (HT, VT) = ರೇಖೆಯ ಜಾಡುಗಳು (HT, VT)
Geometric constructions = ಜ್ಯಾಮಿತೀಯ ರಚನೆಗಳು
Bisector of a line or an angle = ರೇಖೆ ಅಥವಾ ಕೋನದ ದ್ವಿಭಾಜಕ
Parallel through a point = ಬಿಂದುವಿನ ಮೂಲಕ ಸಮಾಂತರ ರೇಖೆ
Perpendicular from a point = ಬಿಂದುವಿನಿಂದ ಲಂಬ
Tangents from a point to a circle = ಬಿಂದುವಿನಿಂದ ವೃತ್ತಕ್ಕೆ ಸ್ಪರ್ಶಕಗಳು
Divide into equal parts = ಸಮ ಭಾಗಗಳಾಗಿ ವಿಭಜಿಸಿ
Measure off equal lengths = ಸಮ ಉದ್ದಗಳನ್ನು ಗುರುತಿಸಿ
Plain or diagonal scale (RF) = ಸರಳ ಅಥವಾ ಕರ್ಣ ಮಾಪಕ (RF)
Development of surfaces = ಮೇಲ್ಮೈಗಳ ವಿಕಸನ
Transition piece (square to round) = ಪರಿವರ್ತನಾ ತುಂಡು (ಚೌಕದಿಂದ ವೃತ್ತಕ್ಕೆ)
Intersection of solids = ಘನಗಳ ಛೇದನ
Perspective of a block = ಘನದ ಪರಿಪ್ರೇಕ್ಷ್ಯ ಚಿತ್ರ
Cutting plane line (A-A) = ಕತ್ತರಿಸುವ ಸಮತಲ ರೇಖೆ (A-A)
True shape of a section = ಛೇದದ ನಿಜವಾದ ಆಕಾರ
First-angle projection symbol = ಪ್ರಥಮ ಕೋನ ಪ್ರಕ್ಷೇಪಣದ ಚಿಹ್ನೆ
BIS (SP 46) drawing setup = BIS (SP 46) ಚಿತ್ರ ಸಿದ್ಧತೆ
Engineering curves = ಎಂಜಿನಿಯರಿಂಗ್ ವಕ್ರಗಳು
Conic by eccentricity (ellipse, parabola, hyperbola) = ಉತ್ಕೇಂದ್ರತೆಯಿಂದ ಶಂಕುಜ (ದೀರ್ಘವೃತ್ತ, ಪರವಲಯ, ಅತಿಪರವಲಯ)
Cycloid = ಸೈಕ್ಲಾಯ್ಡ್
Epicycloid = ಎಪಿಸೈಕ್ಲಾಯ್ಡ್
Hypocycloid = ಹೈಪೋಸೈಕ್ಲಾಯ್ಡ್
Involute of a circle or polygon = ವೃತ್ತ ಅಥವಾ ಬಹುಭುಜಾಕೃತಿಯ ಇನ್ವಾಲ್ಯೂಟ್
Archimedean spiral = ಆರ್ಕಿಮಿಡೀಸ್ ಸುರುಳಿ
Helix = ಹೆಲಿಕ್ಸ್ (ಸುರುಳಿ)
Tangent and normal at a point = ಬಿಂದುವಿನಲ್ಲಿ ಸ್ಪರ್ಶಕ ಮತ್ತು ಅಭಿಲಂಬ
Symbols (electrical, logic, flowchart, network, civil)... = ಚಿಹ್ನೆಗಳು (ವಿದ್ಯುತ್, ತರ್ಕ, ಫ್ಲೋಚಾರ್ಟ್, ನೆಟ್‌ವರ್ಕ್, ಸಿವಿಲ್)...
Wire / connector (stays joined) = ತಂತಿ / ಜೋಡಕ (ಜೋಡಣೆ ಉಳಿಯುತ್ತದೆ)
# Tools and help
Command aliases (my shortcuts)... = ಆದೇಶಗಳ ಕಿರುಹೆಸರುಗಳು (ನನ್ನ ಶಾರ್ಟ್‌ಕಟ್‌ಗಳು)...
Commands... = ಎಲ್ಲಾ ಆದೇಶಗಳು...
Mouse and keyboard tips = ಮೌಸ್ ಮತ್ತು ಕೀಬೋರ್ಡ್ ಸಲಹೆಗಳು
Examples: solved problems with their steps... = ಉದಾಹರಣೆಗಳು: ಹಂತಗಳೊಂದಿಗೆ ಬಿಡಿಸಿದ ಲೆಕ್ಕಗಳು...
About Feather CAD = Feather CAD ಬಗ್ಗೆ
)";

std::wstring g_code = L"en";
std::map<std::wstring, std::wstring> g_table;

std::wstring Trimmed(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == 0xFEFF)) ++a;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r')) --b;
    return s.substr(a, b - a);
}

// A menu text without its "&" marks ("&&" is a real "&").
std::wstring Plain(const std::wstring& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'&') {
            if (i + 1 < s.size() && s[i + 1] == L'&') out += L'&', ++i;
            continue;
        }
        out += s[i];
    }
    return out;
}
}  // namespace

const std::vector<Language>& Languages() {
    static const std::vector<Language> list = {
        {L"en", L"English", L"English"},
        {L"hi", L"Hindi", L"\x0939\x093F\x0928\x094D\x0926\x0940"},
        {L"kn", L"Kannada", L"\x0C95\x0CA8\x0CCD\x0CA8\x0CA1"},
    };
    return list;
}

size_t ReadTranslations(const std::string& utf8, std::map<std::wstring, std::wstring>& out) {
    const std::wstring text = TextFromUtf8(utf8);
    size_t n = 0, start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        const std::wstring line = Trimmed(text.substr(start, end - start));
        start = end + 1;
        if (line.empty() || line[0] == L'#') continue;
        // The last " = " parts the two (English may hold "=" itself, rarely).
        const size_t eq = line.rfind(L" = ");
        if (eq == std::wstring::npos) continue;
        const std::wstring en = Trimmed(line.substr(0, eq)), tr = Trimmed(line.substr(eq + 3));
        if (en.empty() || tr.empty()) continue;
        out[en] = tr;
        ++n;
    }
    return n;
}

void SetLanguage(const std::wstring& code, const std::string& extra) {
    g_table.clear();
    g_code = L"en";
    if (code == L"hi") ReadTranslations(kHindi, g_table), g_code = code;
    if (code == L"kn") ReadTranslations(kKannada, g_table), g_code = code;
    if (g_code != L"en" && !extra.empty()) ReadTranslations(extra, g_table);
}

const std::wstring& CurrentLanguage() { return g_code; }

std::wstring Tr(const std::wstring& english) {
    if (g_table.empty()) return english;
    const size_t tab = english.find(L'\t');
    const std::wstring label = english.substr(0, tab);
    auto it = g_table.find(Plain(label));
    if (it == g_table.end()) return english;
    std::wstring out;
    for (wchar_t c : it->second) {  // a "&" in the translation is shown as it is
        out += c;
        if (c == L'&') out += L'&';
    }
    return tab == std::wstring::npos ? out : out + english.substr(tab);
}

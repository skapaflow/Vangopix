#ifndef VANGOPIX_VERSION_H
#define VANGOPIX_VERSION_H

/*
 * WHAT THIS BUILD CALLS ITSELF, IN THE ONE PLACE BOTH READERS CAN REACH.
 *
 * There are two of them and they are not alike. The splash prints a STRING; the Windows
 * resource wants FOUR COMMA-SEPARATED NUMBERS for its FILEVERSION field, which is not a
 * string and cannot be parsed out of one. That is the whole reason this file exists: two
 * shapes of the same fact, stated once, beside each other, where changing one without the
 * other is visibly wrong.
 *
 * IT HAS ALREADY GONE WRONG ONCE, which is why it is not just a comment asking people to be
 * careful. icon/recicon.rc shipped saying FILEVERSION 3,3,0,0 next to its own "FileVersion"
 * string of "1.0" - two claims in ONE file, disagreeing with each other, and with the header
 * that the program itself reads. Windows shows the resource in a file's Properties, so the
 * copy nobody was maintaining was the copy everybody would see.
 *
 * NOTHING BUT PREPROCESSOR MAY GO IN HERE. windres reads this file too, and it understands
 * #define and nothing else - no types, no declarations, no SDL. That constraint is what keeps
 * the file small enough to be obviously right.
 *
 * WHEN YOU RAISE IT, raise both lines and the git tag together. The tag is what names a
 * published archive; these two are what the program says about itself once somebody has it.
 */
#define VNG_VERSION    "1.0"
#define VNG_VERSION_N   1,0,0,0

#endif

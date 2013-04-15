/*
  ==============================================================================

   This file is part of the JUCE library - "Jules' Utility Class Extensions"
   Copyright 2004-11 by Raw Material Software Ltd.

  ------------------------------------------------------------------------------

   JUCE can be redistributed and/or modified under the terms of the GNU General
   Public License (Version 2), as published by the Free Software Foundation.
   A copy of the license is included in the JUCE distribution, or can be found
   online at www.gnu.org/licenses.

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.rawmaterialsoftware.com/juce for more information.

  ==============================================================================
*/

//==============================================================================
#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD) \
 STATICMETHOD (create,          "create",           "(Ljava/lang/String;I)Landroid/graphics/Typeface;") \
 STATICMETHOD (createFromFile,  "createFromFile",   "(Ljava/lang/String;)Landroid/graphics/Typeface;") \

DECLARE_JNI_CLASS (TypefaceClass, "android/graphics/Typeface");
#undef JNI_CLASS_MEMBERS

#if JUCE_USE_FREETYPE_ANDROID
struct FTLibWrapper     : public ReferenceCountedObject
{
    FTLibWrapper() : library (0)
    {
        if (FT_Init_FreeType (&library) != 0)
        {
            library = 0;
            DBG ("Failed to initialize FreeType");
        }
    }

    ~FTLibWrapper()
    {
        if (library != 0)
            FT_Done_FreeType (library);
    }

    FT_Library library;

    typedef ReferenceCountedObjectPtr <FTLibWrapper> Ptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FTLibWrapper)
};

//==============================================================================
struct FTFaceWrapper     : public ReferenceCountedObject
{
    FTFaceWrapper (const FTLibWrapper::Ptr& ftLib, const File& file, int faceIndex)
        : face (0), library (ftLib)
    {
        if (FT_New_Face (ftLib->library, file.getFullPathName().toUTF8(), faceIndex, &face) != 0)
            face = 0;
    }

    ~FTFaceWrapper()
    {
        if (face != 0)
            FT_Done_Face (face);
    }

    FT_Face face;
    FTLibWrapper::Ptr library;

    typedef ReferenceCountedObjectPtr <FTFaceWrapper> Ptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FTFaceWrapper)
};

//==============================================================================
class AndroidFontFileIterator
{
public:
    AndroidFontFileIterator()
        : index (-1)
    {
        File ("/system/fonts").findChildFiles (fonts, File::findFiles, false, "*.ttf");
    }

    bool next()
    {
        index++;

        if (index >= fonts.size())
            return false;

        return true;
    }

    File getFile() const    { jassert (fonts[index].existsAsFile()); return fonts[index]; }

private:
    Array<File> fonts;
    int index;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AndroidFontFileIterator)
};

//==============================================================================
class FTTypefaceList  : private DeletedAtShutdown
{
public:
    FTTypefaceList()
        : library (new FTLibWrapper())
    {
        AndroidFontFileIterator fontFileIterator;

        while (fontFileIterator.next())
        {
            int faceIndex = 0;
            int numFaces = 0;

            do
            {
                FTFaceWrapper face (library, fontFileIterator.getFile(), faceIndex);

                if (face.face != 0)
                {
                    if (faceIndex == 0)
                        numFaces = face.face->num_faces;

                    if ((face.face->face_flags & FT_FACE_FLAG_SCALABLE) != 0)
                        faces.add (new KnownTypeface (fontFileIterator.getFile(), faceIndex, face));
                }

                ++faceIndex;
            }
            while (faceIndex < numFaces);
        }
    }

    ~FTTypefaceList()
    {
        clearSingletonInstance();
    }

    //==============================================================================
    struct KnownTypeface
    {
        KnownTypeface (const File& f, const int index, const FTFaceWrapper& face)
           : file (f),
             family (face.face->family_name),
             style (face.face->style_name),
             faceIndex (index),
             isMonospaced ((face.face->face_flags & FT_FACE_FLAG_FIXED_WIDTH) != 0),
             isSansSerif (isFaceSansSerif (family))
        {
        }

        const File file;
        const String family, style;
        const int faceIndex;
        const bool isMonospaced, isSansSerif;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnownTypeface)
    };

    //==============================================================================
    FTFaceWrapper::Ptr createFace (const String& fontName, const String& fontStyle)
    {
        const KnownTypeface* ftFace = matchTypeface (fontName, fontStyle);

        if (ftFace == nullptr)  ftFace = matchTypeface (fontName, "Regular");
        if (ftFace == nullptr)  ftFace = matchTypeface (fontName, String::empty);

        if (ftFace != nullptr)
        {
            if (FTFaceWrapper::Ptr face = new FTFaceWrapper (library, ftFace->file, ftFace->faceIndex))
            {
                // If there isn't a unicode charmap then select the first one.
                if (FT_Select_Charmap (face->face, ft_encoding_unicode) != 0)
                    FT_Set_Charmap (face->face, face->face->charmaps[0]);

                return face;
            }
        }

        return nullptr;
    }

    //==============================================================================
    StringArray findAllFamilyNames() const
    {
        StringArray s;

        for (int i = 0; i < faces.size(); i++)
            s.addIfNotAlreadyThere (faces.getUnchecked(i)->family);

        return s;
    }

    StringArray findAllTypefaceStyles (const String& family) const
    {
        StringArray s;

        for (int i = 0; i < faces.size(); i++)
        {
            const KnownTypeface* const face = faces.getUnchecked(i);

            if (face->family == family)
                s.addIfNotAlreadyThere (face->style);
        }

        return s;
    }

    void getMonospacedNames (StringArray& monoSpaced) const
    {
        for (int i = 0; i < faces.size(); i++)
            if (faces.getUnchecked(i)->isMonospaced)
                monoSpaced.addIfNotAlreadyThere (faces.getUnchecked(i)->family);
    }

    void getSerifNames (StringArray& serif) const
    {
        for (int i = 0; i < faces.size(); i++)
            if (! faces.getUnchecked(i)->isSansSerif)
                serif.addIfNotAlreadyThere (faces.getUnchecked(i)->family);
    }

    void getSansSerifNames (StringArray& sansSerif) const
    {
        for (int i = 0; i < faces.size(); i++)
            if (faces.getUnchecked(i)->isSansSerif)
                sansSerif.addIfNotAlreadyThere (faces.getUnchecked(i)->family);
    }

    juce_DeclareSingleton_SingleThreaded_Minimal (FTTypefaceList);

private:
    FTLibWrapper::Ptr library;
    OwnedArray<KnownTypeface> faces;

    const KnownTypeface* matchTypeface (const String& familyName, const String& style) const noexcept
    {
        for (int i = 0; i < faces.size(); ++i)
        {
            const KnownTypeface* const face = faces.getUnchecked(i);

            if (face->family == familyName
                  && (face->style.equalsIgnoreCase (style) || style.isEmpty()))
                return face;
        }

        return nullptr;
    }

    static bool isFaceSansSerif (const String& family)
    {
        const char* sansNames[] = { "Sans", "Verdana", "Arial", "Ubuntu" };

        for (int i = 0; i < numElementsInArray (sansNames); ++i)
            if (family.containsIgnoreCase (sansNames[i]))
                return true;

        return false;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FTTypefaceList)
};

juce_ImplementSingleton_SingleThreaded (FTTypefaceList)


//==============================================================================
class FreeTypeTypeface   : public CustomTypeface
{
public:
    FreeTypeTypeface (const Font& font)
        : faceWrapper (FTTypefaceList::getInstance()
                           ->createFace (font.getTypefaceName(), font.getTypefaceStyle()))
    {
        if (faceWrapper != nullptr)
        {
            setCharacteristics (font.getTypefaceName(),
                                font.getTypefaceStyle(),
                                faceWrapper->face->ascender / (float) (faceWrapper->face->ascender - faceWrapper->face->descender),
                                L' ');
        }
        else
        {
            DBG ("Failed to create typeface: " << font.toString());
        }
    }

    bool loadGlyphIfPossible (const juce_wchar character)
    {
        if (faceWrapper != nullptr)
        {
            FT_Face face = faceWrapper->face;
            const unsigned int glyphIndex = FT_Get_Char_Index (face, character);

            if (FT_Load_Glyph (face, glyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP | FT_LOAD_IGNORE_TRANSFORM) == 0
                  && face->glyph->format == ft_glyph_format_outline)
            {
                const float scale = 1.0f / (float) (face->ascender - face->descender);
                Path destShape;

                if (getGlyphShape (destShape, face->glyph->outline, scale))
                {
                    addGlyph (character, destShape, face->glyph->metrics.horiAdvance * scale);

                    if ((face->face_flags & FT_FACE_FLAG_KERNING) != 0)
                        addKerning (face, character, glyphIndex);

                    return true;
                }
            }
        }

        return false;
    }

private:
    FTFaceWrapper::Ptr faceWrapper;

    bool getGlyphShape (Path& destShape, const FT_Outline& outline, const float scaleX)
    {
        const float scaleY = -scaleX;
        const short* const contours = outline.contours;
        const char* const tags = outline.tags;
        const FT_Vector* const points = outline.points;

        for (int c = 0; c < outline.n_contours; ++c)
        {
            const int startPoint = (c == 0) ? 0 : contours [c - 1] + 1;
            const int endPoint = contours[c];

            for (int p = startPoint; p <= endPoint; ++p)
            {
                const float x = scaleX * points[p].x;
                const float y = scaleY * points[p].y;

                if (p == startPoint)
                {
                    if (FT_CURVE_TAG (tags[p]) == FT_Curve_Tag_Conic)
                    {
                        float x2 = scaleX * points [endPoint].x;
                        float y2 = scaleY * points [endPoint].y;

                        if (FT_CURVE_TAG (tags[endPoint]) != FT_Curve_Tag_On)
                        {
                            x2 = (x + x2) * 0.5f;
                            y2 = (y + y2) * 0.5f;
                        }

                        destShape.startNewSubPath (x2, y2);
                    }
                    else
                    {
                        destShape.startNewSubPath (x, y);
                    }
                }

                if (FT_CURVE_TAG (tags[p]) == FT_Curve_Tag_On)
                {
                    if (p != startPoint)
                        destShape.lineTo (x, y);
                }
                else if (FT_CURVE_TAG (tags[p]) == FT_Curve_Tag_Conic)
                {
                    const int nextIndex = (p == endPoint) ? startPoint : p + 1;
                    float x2 = scaleX * points [nextIndex].x;
                    float y2 = scaleY * points [nextIndex].y;

                    if (FT_CURVE_TAG (tags [nextIndex]) == FT_Curve_Tag_Conic)
                    {
                        x2 = (x + x2) * 0.5f;
                        y2 = (y + y2) * 0.5f;
                    }
                    else
                    {
                        ++p;
                    }

                    destShape.quadraticTo (x, y, x2, y2);
                }
                else if (FT_CURVE_TAG (tags[p]) == FT_Curve_Tag_Cubic)
                {
                    const int next1 = p + 1;
                    const int next2 = (p == (endPoint - 1)) ? startPoint : (p + 2);

                    if (p >= endPoint
                         || FT_CURVE_TAG (tags[next1]) != FT_Curve_Tag_Cubic
                         || FT_CURVE_TAG (tags[next2]) != FT_Curve_Tag_On)
                        return false;

                    const float x2 = scaleX * points [next1].x;
                    const float y2 = scaleY * points [next1].y;
                    const float x3 = scaleX * points [next2].x;
                    const float y3 = scaleY * points [next2].y;

                    destShape.cubicTo (x, y, x2, y2, x3, y3);
                    p += 2;
                }
            }

            destShape.closeSubPath();
        }

        return true;
    }

    void addKerning (FT_Face face, const uint32 character, const uint32 glyphIndex)
    {
        const float height = (float) (face->ascender - face->descender);

        uint32 rightGlyphIndex;
        uint32 rightCharCode = FT_Get_First_Char (face, &rightGlyphIndex);

        while (rightGlyphIndex != 0)
        {
            FT_Vector kerning;

            if (FT_Get_Kerning (face, glyphIndex, rightGlyphIndex, ft_kerning_unscaled, &kerning) == 0
                   && kerning.x != 0)
                addKerningPair (character, rightCharCode, kerning.x / height);

            rightCharCode = FT_Get_Next_Char (face, rightCharCode, &rightGlyphIndex);
        }
    }

    JUCE_DECLARE_NON_COPYABLE (FreeTypeTypeface)
};

//==============================================================================
StringArray Font::findAllTypefaceNames()
{
    return FTTypefaceList::getInstance()->findAllFamilyNames();
}

StringArray Font::findAllTypefaceStyles (const String& family)
{
    return FTTypefaceList::getInstance()->findAllTypefaceStyles (family);
}

//==============================================================================
Typeface::Ptr Typeface::createSystemTypefaceFor (const Font& font)
{
    return new FreeTypeTypeface (font);
}

#else

//==============================================================================
StringArray Font::findAllTypefaceNames()
{
    StringArray results;

    Array<File> fonts;
    File ("/system/fonts").findChildFiles (fonts, File::findFiles, false, "*.ttf");

    for (int i = 0; i < fonts.size(); ++i)
        results.addIfNotAlreadyThere (fonts.getReference(i).getFileNameWithoutExtension()
                                        .upToLastOccurrenceOf ("-", false, false));

    return results;
}

StringArray Font::findAllTypefaceStyles (const String& family)
{
    StringArray results ("Regular");

    Array<File> fonts;
    File ("/system/fonts").findChildFiles (fonts, File::findFiles, false, family + "-*.ttf");

    for (int i = 0; i < fonts.size(); ++i)
        results.addIfNotAlreadyThere (fonts.getReference(i).getFileNameWithoutExtension()
                                        .fromLastOccurrenceOf ("-", false, false));

    return results;
}

//==============================================================================
class AndroidTypeface   : public Typeface
{
public:
    AndroidTypeface (const Font& font)
        : Typeface (font.getTypefaceName(), font.getTypefaceStyle()),
          ascent (0), descent (0), heightToPointsFactor (1.0f)
    {
        JNIEnv* const env = getEnv();

        const bool isBold   = style.contains ("Bold");
        const bool isItalic = style.contains ("Italic");

        File fontFile (getFontFile (name, style));

        if (! fontFile.exists())
            fontFile = findFontFile (name, isBold, isItalic);

        if (fontFile.exists())
            typeface = GlobalRef (env->CallStaticObjectMethod (TypefaceClass, TypefaceClass.createFromFile,
                                                               javaString (fontFile.getFullPathName()).get()));
        else
            typeface = GlobalRef (env->CallStaticObjectMethod (TypefaceClass, TypefaceClass.create,
                                                               javaString (getName()).get(),
                                                               (isBold ? 1 : 0) + (isItalic ? 2 : 0)));

        rect = GlobalRef (env->NewObject (RectClass, RectClass.constructor, 0, 0, 0, 0));

        paint = GlobalRef (GraphicsHelpers::createPaint (Graphics::highResamplingQuality));
        const LocalRef<jobject> ignored (paint.callObjectMethod (Paint.setTypeface, typeface.get()));

        paint.callVoidMethod (Paint.setTextSize, referenceFontSize);

        const float fullAscent = std::abs (paint.callFloatMethod (Paint.ascent));
        const float fullDescent = paint.callFloatMethod (Paint.descent);
        const float totalHeight = fullAscent + fullDescent;

        ascent  = fullAscent / totalHeight;
        descent = fullDescent / totalHeight;
        heightToPointsFactor = referenceFontSize / totalHeight;
    }

    float getAscent() const                 { return ascent; }
    float getDescent() const                { return descent; }
    float getHeightToPointsFactor() const   { return heightToPointsFactor; }

    float getStringWidth (const String& text)
    {
        JNIEnv* env = getEnv();
        const int numChars = text.length();
        jfloatArray widths = env->NewFloatArray (numChars);

        const int numDone = paint.callIntMethod (Paint.getTextWidths, javaString (text).get(), widths);

        HeapBlock<jfloat> localWidths (numDone);
        env->GetFloatArrayRegion (widths, 0, numDone, localWidths);
        env->DeleteLocalRef (widths);

        float x = 0;
        for (int i = 0; i < numDone; ++i)
            x += localWidths[i];

        return x * referenceFontToUnits;
    }

    void getGlyphPositions (const String& text, Array<int>& glyphs, Array<float>& xOffsets)
    {
        JNIEnv* env = getEnv();
        const int numChars = text.length();
        jfloatArray widths = env->NewFloatArray (numChars);

        const int numDone = paint.callIntMethod (Paint.getTextWidths, javaString (text).get(), widths);

        HeapBlock<jfloat> localWidths (numDone);
        env->GetFloatArrayRegion (widths, 0, numDone, localWidths);
        env->DeleteLocalRef (widths);

        String::CharPointerType s (text.getCharPointer());

        xOffsets.add (0);

        float x = 0;
        for (int i = 0; i < numDone; ++i)
        {
            glyphs.add ((int) s.getAndAdvance());
            x += localWidths[i];
            xOffsets.add (x * referenceFontToUnits);
        }
    }

    bool getOutlineForGlyph (int /*glyphNumber*/, Path& /*destPath*/)
    {
        return false;
    }

    EdgeTable* getEdgeTableForGlyph (int glyphNumber, const AffineTransform& t)
    {
        JNIEnv* env = getEnv();

        jobject matrix = GraphicsHelpers::createMatrix (env, AffineTransform::scale (referenceFontToUnits).followedBy (t));
        jintArray maskData = (jintArray) android.activity.callObjectMethod (JuceAppActivity.renderGlyph, (jchar) glyphNumber, paint.get(), matrix, rect.get());

        env->DeleteLocalRef (matrix);

        const int left   = env->GetIntField (rect.get(), RectClass.left);
        const int top    = env->GetIntField (rect.get(), RectClass.top);
        const int right  = env->GetIntField (rect.get(), RectClass.right);
        const int bottom = env->GetIntField (rect.get(), RectClass.bottom);

        const Rectangle<int> bounds (left, top, right - left, bottom - top);

        EdgeTable* et = nullptr;

        if (! bounds.isEmpty())
        {
            et = new EdgeTable (bounds);

            jint* const maskDataElements = env->GetIntArrayElements (maskData, 0);
            const jint* mask = maskDataElements;

            for (int y = top; y < bottom; ++y)
            {
               #if JUCE_LITTLE_ENDIAN
                const uint8* const lineBytes = ((const uint8*) mask) + 3;
               #else
                const uint8* const lineBytes = (const uint8*) mask;
               #endif

                et->clipLineToMask (left, y, lineBytes, 4, bounds.getWidth());
                mask += bounds.getWidth();
            }

            env->ReleaseIntArrayElements (maskData, maskDataElements, 0);
        }

        env->DeleteLocalRef (maskData);
        return et;
    }

    GlobalRef typeface, paint, rect;
    float ascent, descent, heightToPointsFactor;

private:
    static File findFontFile (const String& family,
                              const bool bold, const bool italic)
    {
        File file;

        if (bold || italic)
        {
            String suffix;
            if (bold)   suffix = "Bold";
            if (italic) suffix << "Italic";

            file = getFontFile (family, suffix);

            if (file.exists())
                return file;
        }

        file = getFontFile (family, "Regular");

        if (! file.exists())
            file = getFontFile (family, String::empty);

        return file;
    }

    static File getFontFile (const String& family, const String& style)
    {
        String path ("/system/fonts/" + family);

        if (style.isNotEmpty())
            path << '-' << style;

        return File (path + ".ttf");
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AndroidTypeface)
};

//==============================================================================
Typeface::Ptr Typeface::createSystemTypefaceFor (const Font& font)
{
    return new AndroidTypeface (font);
}

#endif

struct DefaultFontNames
{
    DefaultFontNames()
        : defaultSans  ("sans"),
          defaultSerif ("serif"),
          defaultFixed ("monospace"),
          defaultFallback ("sans")
    {
    }

    String defaultSans, defaultSerif, defaultFixed, defaultFallback;
};

Typeface::Ptr Font::getDefaultTypefaceForFont (const Font& font)
{
    static DefaultFontNames defaultNames;

    String faceName (font.getTypefaceName());

    if (faceName == Font::getDefaultSansSerifFontName())       faceName = defaultNames.defaultSans;
    else if (faceName == Font::getDefaultSerifFontName())      faceName = defaultNames.defaultSerif;
    else if (faceName == Font::getDefaultMonospacedFontName()) faceName = defaultNames.defaultFixed;

    Font f (font);
    f.setTypefaceName (faceName);
    return Typeface::createSystemTypefaceFor (f);
}

const float referenceFontSize = 256.0f;
const float referenceFontToUnits = 1.0f / referenceFontSize;

bool TextLayout::createNativeLayout (const AttributedString&)
{
    return false;
}
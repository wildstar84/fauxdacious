/*
 * tuple.c
 * Copyright 2007-2013 William Pitcock, Christian Birchinger, Matti Hämäläinen,
 *                     Giacomo Lozito, Eugene Zagidullin, and John Lindgren
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>  /* for g_utf8_validate */

#include "audio.h"
#include "audstrings.h"
#include "i18n.h"
#include "tuple.h"
#include "vfs.h"
#include "runtime.h"

enum {
    FallbackTitle = Tuple::n_fields,
    FallbackArtist,
    FallbackAlbum,

    n_private_fields
};

static_assert (n_private_fields <= 64,
 "The current tuple implementation is limited to 64 fields");

union TupleVal
{
    String str;
    int x;

    // dummy constructor and destructor
    TupleVal () {}
    ~TupleVal () {}
};

/**
 * Structure for holding and passing around miscellaneous track
 * metadata. This is not the same as a playlist entry, though.
 */
struct TupleData
{
    uint64_t setmask;      // which fields are present
    Index<TupleVal> vals;  // ordered list of field values

    short * subtunes;               /**< Array of int containing subtune index numbers.
                                         Can be nullptr if indexing is linear or if
                                         there are no subtunes. */
    short nsubtunes;                /**< Number of subtunes, if any. Values greater than 0
                                         mean that there are subtunes and #subtunes array
                                         may be set. */

    short state;
    int refcount;

    TupleData ();
    ~TupleData ();

    TupleData (const TupleData & other);
    void operator= (const TupleData & other) = delete;

    bool is_set (int field) const
        { return (setmask & bitmask (field)); }

    bool is_same (const TupleData & other) const;

    TupleVal * lookup (int field, bool add, bool remove);
    void set_int (int field, int x);
    void set_str (int field, const char * str);
    void set_subtunes (short nsubs, const short * subs);

    static TupleData * ref (TupleData * tuple);
    static void unref (TupleData * tuple);

    static TupleData * copy_on_write (TupleData * tuple);

private:
    static constexpr uint64_t bitmask (int n)
        { return (uint64_t) 1 << n; }
};

/** Ordered table of basic #Tuple field names and their #ValueType.
 */
static const struct {
    const char * name;
    Tuple::ValueType type;
    int fallback;
} field_info[] = {
    {"title", Tuple::String, FallbackTitle},
    {"artist", Tuple::String, FallbackArtist},
    {"album", Tuple::String, FallbackAlbum},
    {"album-artist", Tuple::String, -1},
    {"comment", Tuple::String, -1},
    {"genre", Tuple::String, -1},
    {"year", Tuple::Int, -1},

    {"composer", Tuple::String, -1},
    {"performer", Tuple::String, -1},
    {"copyright", Tuple::String, -1},
    {"date", Tuple::String, -1},

    {"track-number", Tuple::Int, -1},
    {"length", Tuple::Int, -1},

    {"bitrate", Tuple::Int, -1},
    {"codec", Tuple::String, -1},
    {"quality", Tuple::String, -1},

    {"file-name", Tuple::String, -1},
    {"file-path", Tuple::String, -1},
    {"file-ext", Tuple::String, -1},

    {"audio-file", Tuple::String, -1},

    {"subsong-id", Tuple::Int, -1},
    {"subsong-num", Tuple::Int, -1},

    {"segment-start", Tuple::Int, -1},
    {"segment-end", Tuple::Int, -1},

    {"gain-album-gain", Tuple::Int, -1},
    {"gain-album-peak", Tuple::Int, -1},
    {"gain-track-gain", Tuple::Int, -1},
    {"gain-track-peak", Tuple::Int, -1},
    {"gain-gain-unit", Tuple::Int, -1},
    {"gain-peak-unit", Tuple::Int, -1},

    {"formatted-title", Tuple::String, -1},
    {"description", Tuple::String, -1},
    {"musicbrainz-id", Tuple::String, -1},
    {"lyrics", Tuple::String, -1},
    {"channels", Tuple::Int, -1},
    {"publisher", Tuple::String, -1},
    {"catalog-number", Tuple::String, -1},
    {"disc-number", Tuple::Int, -1},

    /* fallbacks */
    {nullptr, Tuple::String, -1},
    {nullptr, Tuple::String, -1},
    {nullptr, Tuple::String, -1},
};

static_assert (aud::n_elems (field_info) == n_private_fields, "Update field_data");

struct FieldDictEntry {
    const char * name;
    Tuple::Field field;
};

/* used for binary search, MUST be in alphabetical order */
static const FieldDictEntry field_dict[] = {
    {"album", Tuple::Album},
    {"album-artist", Tuple::AlbumArtist},
    {"artist", Tuple::Artist},
    {"audio-file", Tuple::AudioFile},
    {"bitrate", Tuple::Bitrate},
    {"catalog-number", Tuple::CatalogNum},
    {"channels", Tuple::Channels},
    {"codec", Tuple::Codec},
    {"comment", Tuple::Comment},
    {"composer", Tuple::Composer},
    {"copyright", Tuple::Copyright},
    {"date", Tuple::Date},
    {"description", Tuple::Description},
    {"disc-number", Tuple::Disc},
    {"file-ext", Tuple::Suffix},
    {"file-name", Tuple::Basename},
    {"file-path", Tuple::Path},
    {"formatted-title", Tuple::FormattedTitle},
    {"gain-album-gain", Tuple::AlbumGain},
    {"gain-album-peak", Tuple::AlbumPeak},
    {"gain-gain-unit", Tuple::GainDivisor},
    {"gain-peak-unit", Tuple::PeakDivisor},
    {"gain-track-gain", Tuple::TrackGain},
    {"gain-track-peak", Tuple::TrackPeak},
    {"genre", Tuple::Genre},
    {"length", Tuple::Length},
    {"lyrics", Tuple::Lyrics},
    {"musicbrainz-id", Tuple::MusicBrainzID},
    {"performer", Tuple::Performer},
    {"publisher", Tuple::Publisher},
    {"quality", Tuple::Quality},
    {"segment-end", Tuple::EndTime},
    {"segment-start", Tuple::StartTime},
    {"subsong-id", Tuple::Subtune},
    {"subsong-num", Tuple::NumSubtunes},
    {"title", Tuple::Title},
    {"track-number", Tuple::Track},
    {"year", Tuple::Year}
};

static_assert (aud::n_elems (field_dict) == Tuple::n_fields, "Update field_dict");

static const char * find_domain (const char * name);
static StringBuf extract_domain (const char * start);

static constexpr bool is_valid_field (int field)
    { return field > Tuple::Invalid && field < Tuple::n_fields; }

static int bitcount (uint64_t x)
{
    /* algorithm from http://en.wikipedia.org/wiki/Hamming_weight */
    x -= (x >> 1) & 0x5555555555555555;
    x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333);
    x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0f;
    return (x * 0x0101010101010101) >> 56;
}

static int field_dict_compare (const void * a, const void * b)
{
    return strcmp (((FieldDictEntry *) a)->name, ((FieldDictEntry *) b)->name);
}

EXPORT Tuple::Field Tuple::field_by_name (const char * name)
{
    FieldDictEntry find = {name, Invalid};
    FieldDictEntry * found = (FieldDictEntry *) bsearch (& find, field_dict,
     n_fields, sizeof (FieldDictEntry), field_dict_compare);

    return found ? found->field : Invalid;
}

EXPORT const char * Tuple::field_get_name (Field field)
{
    assert (is_valid_field (field));
    return field_info[field].name;
}

EXPORT Tuple::ValueType Tuple::field_get_type (Field field)
{
    assert (is_valid_field (field));
    return field_info[field].type;
}

TupleVal * TupleData::lookup (int field, bool add, bool remove)
{
    /* calculate number of preceding fields */
    const uint64_t mask = bitmask (field);
    const int pos = bitcount (setmask & (mask - 1));

    if ((setmask & mask))
    {
        if ((add || remove) && field_info[field].type == Tuple::String)
            vals[pos].str.~String ();

        if (remove)
        {
            setmask &= ~mask;
            vals.remove (pos, 1);
            return nullptr;
        }

        return & vals[pos];
    }

    if (! (add || remove) && field_info[field].fallback >= 0)
        return lookup (field_info[field].fallback, false, false);

    if (! add)
        return nullptr;

    setmask |= mask;
    vals.insert (pos, 1);
    return & vals[pos];
}

void TupleData::set_int (int field, int x)
{
    TupleVal * val = lookup (field, true, false);
    val->x = x;
}

void TupleData::set_str (int field, const char * str)
{
    TupleVal * val = lookup (field, true, false);
    new (& val->str) String (str);
}

void TupleData::set_subtunes (short nsubs, const short * subs)
{
    nsubtunes = nsubs;

    delete[] subtunes;
    subtunes = nullptr;

    if (nsubs && subs)
    {
        subtunes = new short[nsubs];
        memcpy (subtunes, subs, sizeof subtunes[0] * nsubs);
    }
}

TupleData::TupleData () :
    setmask (0),
    subtunes (nullptr),
    nsubtunes (0),
    state (Tuple::Initial),
    refcount (1) {}

TupleData::TupleData (const TupleData & other) :
    setmask (other.setmask),
    subtunes (nullptr),
    nsubtunes (0),
    state (other.state),
    refcount (1)
{
    vals.insert (0, other.vals.len ());

    auto get = other.vals.begin ();
    auto set = vals.begin ();

    for (int f = 0; f < n_private_fields; f ++)
    {
        if (other.setmask & bitmask (f))
        {
            if (field_info[f].type == Tuple::String)
                new (& set->str) String (get->str);
            else
                set->x = get->x;

            get ++;
            set ++;
        }
    }

    set_subtunes (other.nsubtunes, other.subtunes);
}

TupleData::~TupleData ()
{
    auto iter = vals.begin ();

    for (int f = 0; f < n_private_fields; f ++)
    {
        if (setmask & bitmask (f))
        {
            if (field_info[f].type == Tuple::String)
                iter->str.~String ();

            iter ++;
        }
    }

    delete[] subtunes;
}

bool TupleData::is_same (const TupleData & other) const
{
    if (state != other.state || setmask != other.setmask ||
     nsubtunes != other.nsubtunes || (! subtunes) != (! other.subtunes))
        return false;

    auto a = vals.begin ();
    auto b = other.vals.begin ();

    for (int f = 0; f < n_private_fields; f ++)
    {
        if (setmask & bitmask (f))
        {
            bool same;

            if (field_info[f].type == Tuple::String)
                same = (a->str == b->str);
            else
                same = (a->x == b->x);

            if (! same)
                return false;

            a ++;
            b ++;
        }
    }

    if (subtunes && memcmp (subtunes, other.subtunes, sizeof subtunes[0] * nsubtunes))
        return false;

    return true;
}

TupleData * TupleData::ref (TupleData * tuple)
{
    if (tuple)
        __sync_fetch_and_add (& tuple->refcount, 1);

    return tuple;
}

void TupleData::unref (TupleData * tuple)
{
    if (tuple && ! __sync_sub_and_fetch (& tuple->refcount, 1))
        delete tuple;
}

TupleData * TupleData::copy_on_write (TupleData * tuple)
{
    if (! tuple)
        return new TupleData;

    if (__sync_fetch_and_add (& tuple->refcount, 0) == 1)
        return tuple;

    TupleData * copy = new TupleData (* tuple);
    unref (tuple);
    return copy;
}

EXPORT Tuple::~Tuple ()
{
    TupleData::unref (data);
}

EXPORT bool Tuple::operator== (const Tuple & b) const
{
    if (data == b.data)
        return true;

    if (! data || ! b.data)
        return false;

    return data->is_same (* b.data);
}

EXPORT Tuple Tuple::ref () const
{
    Tuple tuple;
    tuple.data = TupleData::ref (data);
    return tuple;
}

EXPORT Tuple::State Tuple::state () const
{
    return data ? (Tuple::State) data->state : Initial;
}

EXPORT void Tuple::set_state (State st)
{
    data = TupleData::copy_on_write (data);
    data->state = st;
}

EXPORT Tuple::ValueType Tuple::get_value_type (Field field) const
{
    assert (is_valid_field (field));

    const auto & info = field_info[field];
    if (data && (data->is_set (field) || (info.fallback >= 0 && data->is_set (info.fallback))))
        return info.type;

    return Empty;
}

EXPORT bool Tuple::is_fallback (Field field) const
{
    assert (is_valid_field (field));

    if (field_info[field].fallback < 0)
        return false;

    TupleVal * val = data ? data->lookup (field, false, false) : nullptr;
    if (! val)
        return false;

    TupleVal * fbval = data->lookup (field_info[field].fallback, false, false);
    if (! fbval)
        return false;

    if (field_info[field].type == Int)
        return (val->x == fbval->x);
    else
        return (val->str == fbval->str);
}

EXPORT int Tuple::get_int (Field field) const
{
    assert (is_valid_field (field) && field_info[field].type == Int);

    TupleVal * val = data ? data->lookup (field, false, false) : nullptr;
    return val ? val->x : -1;
}

EXPORT String Tuple::get_str (Field field) const
{
    assert (is_valid_field (field) && field_info[field].type == String);

    TupleVal * val = data ? data->lookup (field, false, false) : nullptr;
    return val ? val->str : ::String ();
}

EXPORT void Tuple::set_int (Field field, int x)
{
    assert (is_valid_field (field) && field_info[field].type == Int);

    data = TupleData::copy_on_write (data);
    data->set_int (field, x);
}

EXPORT void Tuple::set_str (Field field, const char * str)
{
    assert (is_valid_field (field) && field_info[field].type == String);

    if (! str)
    {
        unset (field);
        return;
    }

    data = TupleData::copy_on_write (data);

    if (g_utf8_validate (str, -1, nullptr))
        data->set_str (field, str);
    else
    {
        StringBuf utf8 = str_to_utf8 (str, -1);
        data->set_str (field, utf8 ? (const char *) utf8 : _("(character encoding error)"));
    }
}

EXPORT void Tuple::unset (Field field)
{
    assert (is_valid_field (field));

    if (! data)
        return;

    data = TupleData::copy_on_write (data);
    data->lookup (field, false, true);
}

EXPORT void Tuple::set_filename (const char * filename)
{
    assert (filename);

    data = TupleData::copy_on_write (data);

    // stdin is handled as a special case
    if (! strncmp (filename, "stdin://", 8))
    {
        data->set_str (Basename, _("Standard input"));
        return;
    }

    const char * base, * ext, * sub;
    int isub;

    uri_parse (filename, & base, & ext, & sub, & isub);

    if (base > filename)
        data->set_str (Path, uri_to_display (str_copy (filename, base - filename)));
    if (ext > base)
        data->set_str (Basename, str_to_utf8 (str_decode_percent (base, ext - base)));
    if (sub > ext + 1)
        data->set_str (Suffix, str_to_utf8 (str_decode_percent (ext + 1, sub - ext - 1)));

    if (sub[0])
        data->set_int (Subtune, isub);
}

EXPORT void Tuple::set_format (const char * format, int chans, int rate, int brate)
{
    if (format)
        set_str (Codec, format);

    StringBuf buf;

    if (chans > 0)
    {
        if (chans == 1)
            buf = str_copy (_("Mono"));
        else if (chans == 2)
            buf = str_copy (_("Stereo"));
        else
            buf = str_printf (dngettext (PACKAGE, "%d channel", "%d channels", chans), chans);

        if (rate > 0)
            buf.insert (-1, ", ");
    }

    if (rate > 0)
        str_append_printf (buf, "%d kHz", rate / 1000);

    if (buf.len ())
        set_str (Quality, buf);

    if (brate > 0)
        set_int (Bitrate, brate);

    if (chans > 0)
        set_int (Channels, chans);
}

EXPORT void Tuple::set_subtunes (short n_subtunes, const short * subtunes)
{
    data = TupleData::copy_on_write (data);
    data->set_subtunes (n_subtunes, subtunes);
}

EXPORT short Tuple::get_n_subtunes() const
{
    return data ? data->nsubtunes : 0;
}

EXPORT short Tuple::get_nth_subtune (short n) const
{
    if (! data || n < 0 || n >= data->nsubtunes)
        return -1;

    return data->subtunes ? data->subtunes[n] : 1 + n;
}

EXPORT void Tuple::set_gain (Field field, Field unit_field, const char * str)
{
    set_int (field, lround (str_to_double (str) * 1000000));
    set_int (unit_field, 1000000);
}

/* combining this with get_replay_gain() would be cleaner but would
 * require adding a validity flag to ReplayGainInfo, breaking ABI */
EXPORT bool Tuple::has_replay_gain () const
{
    return get_int (GainDivisor) > 0 &&
           (data->is_set (AlbumGain) || data->is_set (TrackGain));
}

EXPORT ReplayGainInfo Tuple::get_replay_gain () const
{
    ReplayGainInfo gain {};

    if (! data)
        return gain;

    int gain_unit = get_int (GainDivisor);
    int peak_unit = get_int (PeakDivisor);

    if (gain_unit > 0)
    {
        bool have_album = data->is_set (AlbumGain);
        bool have_track = data->is_set (TrackGain);

        if (have_album)
            gain.album_gain = get_int (AlbumGain) / (float) gain_unit;
        if (have_track)
            gain.track_gain = get_int (TrackGain) / (float) gain_unit;

        /* fill in missing information if we can */
        if (! have_album && have_track)
            gain.album_gain = gain.track_gain;
        if (have_album && ! have_track)
            gain.track_gain = gain.album_gain;
    }

    if (peak_unit > 0)
    {
        bool have_album = data->is_set (AlbumPeak);
        bool have_track = data->is_set (TrackPeak);

        if (have_album)
            gain.album_peak = get_int (AlbumPeak) / (float) peak_unit;
        if (have_track)
            gain.track_peak = get_int (TrackPeak) / (float) peak_unit;

        /* fill in missing information if we can */
        if (! have_album && have_track)
            gain.album_peak = gain.track_peak;
        if (have_album && ! have_track)
            gain.track_peak = gain.album_peak;
    }

    return gain;
}

EXPORT bool Tuple::fetch_stream_info (VFSFile & stream)
{
    bool updated = false;
    bool haveArtworkUrl = false;
    int value;
    bool split_titles = aud_get_bool (nullptr, "split_titles");
    bool genreisset = false;

    ::String val = stream.get_metadata ("track-name");
    if (val && val[0] && strncmp ((const char *) val, "  - ", 4))
    {
        if (! fauxd_is_prevmeta (0, val))
        {
            bool albumisset = false;  /* JWT:TRUE IF STREAM METADATA ITSELF CONTAINS AN ALBUM FIELD. */
            fauxd_set_prevmeta (0, val);
            const char * ttloffset = strstr ((const char *) val, " - text=\"");

            aud_set_str (nullptr, "_cover_art_link", "");
            unset (Lyrics); //JWT:REMOVE ANY RESIDUAL "LYRICS" FROM TAGFILE (LIKELY FROM URLHELPER) SO SONG CHG. CAN OVERRIDE!
            if (ttloffset)  //JWT:FIXUP UGLY IHeartRadio STREAM TITLES (EXTRACT TITLE FROM "...-text="TITLE"):
            {
                const char * endquote = strstr (ttloffset+10, "\"");
                if (endquote)
                {
                    const char * ttloffset9 = ttloffset + 9;
                    const char * tupletitle = (const char *) get_str (Title);
                    int artlen = ttloffset - (const char *) val;
                    if (artlen > 0 && (! tupletitle || strncmp ((const char *) val, tupletitle, artlen)))
                    {
                        if (split_titles)
                        {
                            set_str (Title, str_printf ("%.*s", (int)(endquote-ttloffset9), ttloffset9));
                            set_str (Artist, str_printf ("%.*s", artlen, (const char *) val));
                        }
                        else
                        {
                            set_str (Title, str_printf ("%.*s - %.*s", artlen,
                                    (const char *) val, (int) (endquote-ttloffset9), ttloffset9));
                            set_str (Artist, ::String(""));  //JWT:BLANK ARTIST FIELD (AS ARTIST IS IN TITLE)!
                        }
                        updated = true;
                    }

                    /* JWT:SOME iHeart STATIONS INCLUDE COVER-ART!: */
                    const char * coverart = strstr (endquote, "ArtworkURL=\"");
                    if (coverart)
                    {
                        const char * coveroffset = coverart + 12;
                        const char * endquote = strstr (coveroffset, "\"");
                        if (endquote)
                        {
                            int coverlen = endquote - coveroffset;
                            if (coverlen > 0)
                            {
                                aud_set_str (nullptr, "_cover_art_link", str_printf ("%.*s", coverlen, coveroffset));
                                haveArtworkUrl = true;
                            }
                        }
                    }
                }
            }
            else  /* LOOK FOR:  'Title: ... Artist: ...' etc.: */
            {
                ttloffset = strstr_nocase ((const char *) val, "Title: ");
                if (ttloffset)  //JWT:FIXUP STREAM TILES IN FORMAT: "[[Artist: ]artist - ]Title: title [Album: album]:
                {
                    const char * metaindxptr = (const char *) val;
                    const char * metaptr[4];
                    int metaindx = 0;
                    int titleindx = -1;
                    int artistindx = -1;
                    int albumindx = -1;
                    int yearindx = -1;
                    /* JWT:PARSE OUT TITLE, ARTIST, ALBUM, & YEAR PARTS:
                        (NOTE: MAY NOT HAVE 'EM ALL & ORDER MAY VARY) */
                    while (metaindxptr && metaindx < 4)
                    {
                        if (str_has_prefix_nocase (metaindxptr, "Title: "))
                        {
                            metaptr[metaindx] = metaindxptr;
                            titleindx = metaindx++;
                        }
                        else if (str_has_prefix_nocase (metaindxptr, "Artist: "))
                        {
                            metaptr[metaindx] = metaindxptr;
                            artistindx = metaindx++;
                        }
                        else if (str_has_prefix_nocase (metaindxptr, "Album: "))
                        {
                            metaptr[metaindx] = metaindxptr;
                            albumindx = metaindx++;
                        }
                        else if (str_has_prefix_nocase (metaindxptr, "Year: "))
                        {
                            metaptr[metaindx] = metaindxptr;
                            yearindx = metaindx++;
                        }
                        ++metaindxptr;
                    }
                    --metaindx;
                    int artistlen = (metaptr[artistindx] && artistindx < metaindx)
                            ? metaptr[artistindx+1] - (metaptr[artistindx]+11) : -1;
                    int titlelen = (metaptr[titleindx] && titleindx < metaindx)
                            ? metaptr[titleindx+1] - (metaptr[titleindx]+10) : -1;
                    if (split_titles)
                    {
                        set_str (Title, str_copy (metaptr[titleindx]+7, titlelen));
                        if (metaptr[artistindx])
                            set_str (Artist, str_copy (metaptr[artistindx]+8, artistlen));
                    }
                    else
                    {
                        set_str (Artist, ::String(""));  //JWT:BLANK ARTIST FIELD (AS ARTIST IS IN TITLE)!
                        if (metaptr[artistindx])
                            set_str (Title, str_printf ("%.*s%s%.*s", artistlen, metaptr[artistindx]+8,
                                    " - ", titlelen, metaptr[titleindx]+7));
                        else
                            set_str (Title, (const char *) val);

                    }
                    if (metaptr[albumindx])
                    {
                        int albumlen = (metaptr[albumindx] && albumindx < metaindx)
                                ? metaptr[albumindx+1] - (metaptr[albumindx]+10) : -1;

                        albumisset = true;
                        ::String stream_name = stream.get_metadata ("stream-name");
                        if (stream_name && stream_name[0] && stream_name != ::String("(null)"))
                            set_str (Album, str_printf ("%s%s%s",
                                    (const char *) str_copy (metaptr[albumindx]+7, albumlen), " - ",
                                    (const char *) stream_name));
                        else
                            set_str (Album, str_copy (metaptr[albumindx]+7, albumlen));
                    }
                    if (metaptr[yearindx])
                        set_int (Year, atoi (str_copy (metaptr[yearindx]+6, ((yearindx < metaindx)
                                ? (metaptr[yearindx+1] - (metaptr[yearindx]+6)) : -1))));
                }
                else  /* LOOK FOR:  'artist="..." title="..."': */
                {
                    ttloffset = strstr_nocase ((const char *) val, "title=\"");
                    if (ttloffset)
                    {
                        ttloffset += 7;
                        const char * endquote = strstr (ttloffset+1, "\"");
                        if (endquote)
                        {
                            int ttllen = (int) (endquote-ttloffset);
                            const char * artoffset = strstr_nocase ((const char *) val, "artist=\"");
                            if (artoffset)  //WE HAVE A SEPARATE ARTIST FIELD:
                            {
                                artoffset += 8;
                                endquote = strstr (artoffset+1, "\"");
                                if (endquote)
                                {
                                    int artlen = (int) (endquote-artoffset);
                                    if (split_titles)
                                    {
                                        if (ttllen > 0)
                                            set_str (Title, (const char *) str_printf ("%.*s",
                                                    ttllen, ttloffset));
                                        if (artlen > 0)
                                            set_str (Artist, (const char *) str_printf ("%.*s",
                                                    artlen, artoffset));
                                    }
                                    else if (ttllen > 0)
                                        set_str (Title, str_printf ("%.*s - %.*s", artlen,
                                                artoffset, ttllen, ttloffset));
                                }
                            }
                            else if (split_titles)  //NO ARTIST FIELD, CHECK TITLE:
                            {
                                const char * split = strstr (ttloffset, " - ");
                                if (split && endquote > split + 3)
                                {
                                    set_str (Artist, (const char *) str_printf ("%.*s",
                                            (int) (split - ttloffset), ttloffset));
                                    split += 3;
                                    if (endquote > split)
                                        set_str (Title, (const char *) str_printf ("%.*s",
                                                (int) (endquote - split), split));
                                }
                                else if (ttllen > 0)
                                    set_str (Title, (const char *) str_printf ("%.*s",
                                            ttllen, ttloffset));
                            }
                            else
                            {
                                set_str (Artist, ::String(""));  //JWT:BLANK ARTIST FIELD (AS ARTIST IS IN TITLE)!
                                if (ttllen > 0)
                                    set_str (Title, (const char *) str_printf ("%.*s",
                                            ttllen, ttloffset));
                            }
                        }
                        ttloffset = strstr_nocase ((const char *) val, "album=\"");
                        if (ttloffset)  //WE HAVE A SEPARATE ALBUM FIELD:
                        {
                            ttloffset += 7;
                            const char * endquote = strstr (ttloffset+1, "\"");
                            if (endquote)
                            {
                                int albumlen = (int) (endquote-ttloffset);
                                if (albumlen > 0)
                                {
                                    albumisset = true;
                                    ::String stream_name = stream.get_metadata ("stream-name");
                                    if (stream_name && stream_name[0] && stream_name != ::String("(null)"))
                                        set_str (Album, (const char *) str_printf ("%.*s%s%s",
                                                albumlen, ttloffset, " - ",
                                                (const char *) stream_name));
                                    else
                                        set_str (Album, (const char *) str_printf ("%.*s",
                                                albumlen, ttloffset));
                                }
                            }
                        }
                    }
                    else if (split_titles)
                    {
                        const char * artoffset = (const char *) val;
                        const char * ttloffset = strstr (artoffset, " - ");
                        const char * junkoffset = strstr (artoffset, " || "); // JWT:CLEAN UP TITLES W/" || #### S", ETC?!
                        if (ttloffset)
                        {
                            ttloffset += 3;
                            if (junkoffset && junkoffset > ttloffset)
                                set_str (Title, (const char *) str_printf ("%.*s",
                                        (int) (junkoffset-ttloffset), ttloffset));
                            else
                            {
                                const char * extrametadata = strstr (ttloffset, " - ");
                                if (extrametadata)  // JWT:ASSUMING:  "artist - title - album[ - genre]":
                                {
                                    set_str (Title, (const char *) str_printf ("%.*s",
                                            (int) (extrametadata-ttloffset), ttloffset));
                                    const char * album = extrametadata += 3;
                                    if (album)
                                    {
                                        const char * extrametadata = strstr (album, " - ");
										 if (! extrametadata || extrametadata > album )
										 {
											albumisset = true;
											::String stream_name = stream.get_metadata ("stream-name");
											if (stream_name && stream_name[0] && stream_name != ::String("(null)"))
											{
												if (extrametadata)
													set_str (Album, (const char *) str_printf ("%.*s%s%s",
															(int)(extrametadata - album), album, " - ",
															(const char *) stream_name));
												else
													set_str(Album, (const char *) str_printf ("%s%s%s", album,
															" - ", (const char *) stream_name));
											}
											else
											{
												if (extrametadata)
													set_str (Album, (const char *) str_printf ("%.*s",
															(int)(extrametadata - album), album));
												else
													set_str (Album, album);
											}
                                        }
                                        if (extrametadata)
                                        {
                                            const char * genre = extrametadata + 3;
                                            if (genre)
                                            {
                                                set_str (Genre, genre);
                                                genreisset = true;
                                            }
                                        }
                                    }
                                }
                                else
                                    set_str (Title, ttloffset);
                            }
                            set_str (Artist, (const char *) str_printf ("%.*s",
                                    (int) ((ttloffset-artoffset)-3), artoffset));
                        }
                        else if (junkoffset && junkoffset > artoffset)
                            set_str (Title, (const char *) str_printf ("%.*s",
                                    (int) (junkoffset-artoffset), artoffset));
                        else
                            set_str (Title, artoffset);
                    }
                    else
                    {
                        set_str (Artist, ::String(""));  //JWT:BLANK ARTIST FIELD (AS ARTIST IS IN TITLE)!
                        if (val != get_str (Title))
                            set_str (Title, val);
                    }
                }

                updated = true;
            }

            if (! albumisset)
            {
                ::String stream_name = stream.get_metadata ("stream-name");
                if (stream_name && stream_name[0] && stream_name != ::String("(null)"))
                {
                    const char * s;
                    auto streampath = get_str (Path);
                    ::String album = get_str (Album);
                    if (split_titles)
                    {
                        if (!album || !album[0])
                            set_str (Album, stream_name);

                        ::String albumartist = get_str (AlbumArtist);
                        if ((!albumartist || !albumartist[0]) && streampath && (s = find_domain (streampath)))
                            set_str (AlbumArtist, extract_domain (s));  //JWT:MOVE STREAM URL DOMAIN-NAME DOWN TO ALBUMARTIST.
                    }
                    else
                    {
                        set_str (Artist, stream_name);
                        if (!album || !album[0] || album == stream_name)
                        {
                            if (streampath && (s = find_domain (streampath)))
                                set_str (Album, extract_domain (s));
                            set_str (AlbumArtist, ::String (""));
                        }
                    }
                }
                updated = true;
            }
            /* JWT:AVOID DUPLICATES: */
            ::String artist = get_str (Artist);
            ::String album = get_str (Album);
            if (artist == album)
            {
                if (split_titles)
                    set_str (Album, ::String (""));
                else
                    set_str (Artist, ::String (""));
                updated = true;
            }
            if (split_titles)
            {
                ::String albumartist = get_str (AlbumArtist);
                if (albumartist == album)
                {
                    set_str (AlbumArtist, ::String (""));
                    updated = true;
                }
            }

            if (! haveArtworkUrl && aud_get_str (nullptr, "_cover_art_link") == ::String (""))
            {
                /* JWT:SOME WEIRD STREAMS PUT A COVER-ART LINK AS THEIR "STREAM-URL"?! */
                ::String stream_url = stream.get_metadata ("stream-url");
                if (stream_url && stream_url[0])
                {
                    Index<::String> extlist = str_list_to_index ("jpg,png,gif,jpeg,webp", ",");
                    StringBuf uriext = uri_get_extension (stream_url);
                    for (auto & ext : extlist)
                    {
                        if (! strcmp_nocase (uriext, ext))
                        {
                            aud_set_str (nullptr, "_cover_art_link", stream_url);
                            break;
                        }
                    }
                }
            }
        }
    }

    val = stream.get_metadata ("stream-name");
    if (val && val[0] && ! fauxd_is_prevmeta (1, val))
    {
        fauxd_set_prevmeta (1, val);
        if (split_titles)
        {
            ::String tuple_album = get_str (Album);
            if (! tuple_album || tuple_album == ::String("(null)"))
            {
                set_str (Album, val);
                updated = true;
            }
        }
        else if (val != get_str (Artist))
        {
            set_str (Artist, val);
            updated = true;
        }
        if (! genreisset)
        {
            ::String stream_genre = stream.get_metadata ("stream-genre");
            if (stream_genre && stream_genre[0] && stream_genre != get_str (Genre))
            {
                set_str (Genre, stream_genre);
                updated = true;
            }
        }
    }

    val = stream.get_metadata ("content-bitrate");
    value = val ? atoi (val) / 1000 : 0;

    if (value && value != get_int (Bitrate))
    {
        set_int (Bitrate, value);
        updated = true;
    }

    return updated;
}

/* Separates the lowest-level folder from a file path.  The string passed will
 * be modified, and the string returned will use the same memory.  May return
 * nullptr. */

static char * split_folder (char * path, char sep)
{
    char * c;
    while ((c = strrchr (path, sep)))
    {
        * c = 0;
        if (c[1])
            return c + 1;
    }

    return path[0] ? path : nullptr;
}

/* These two functions separate the domain name from an internet URL.  Examples:
 *     "http://some.domain.org/folder/file.mp3" -> "some.domain.org"
 *     "http://some.stream.fm:8000"             -> "some.stream.fm" */

static const char * find_domain (const char * name)
{
    if (! strncmp (name, "http://", 7))
        return name + 7;
    if (! strncmp (name, "https://", 8))
        return name + 8;
    if (! strncmp (name, "mms://", 6))
        return name + 6;

    return nullptr;
}

static StringBuf extract_domain (const char * start)
{
    StringBuf name = str_copy (start);
    char * c;

    if ((c = strchr (name, '/')))
        name.resize (c - name);
    if ((c = strchr (name, ':')))
        name.resize (c - name);
    if ((c = strchr (name, '?')))
        name.resize (c - name);

    return name;
}

EXPORT void Tuple::generate_fallbacks ()
{
    if (! data)
        return;

    generate_title ();

    auto artist = get_str (Artist);
    auto album = get_str (Album);
    auto genre = get_str (Genre);

    if (artist && album)
        return;

    data = TupleData::copy_on_write (data);

    // use album artist, if present
    if (! artist && (artist = get_str (AlbumArtist)))
    {
        data->set_str (FallbackArtist, artist);

        if (album)
            return; // nothing left to do
    }

    auto filepath = get_str (Path);
    if (! filepath)
        return;

    const char * s;
    char sep;

    if (! strcmp (filepath, "cdda://"))
    {
        // audio CD:
        // use "Audio CD" as the album

        if (! album)
            data->set_str (FallbackAlbum, _("Audio CD"));
    }
    else if (! strcmp (filepath, "dvd://"))
    {
        // DVD:
        // use "DVD" as the album

        if (! album)
            data->set_str (FallbackAlbum, _("DVD"));
    }
    else if ((s = find_domain (filepath)))
    {
        // internet URL:
        // use the domain name as the album

        if (! album)
            data->set_str (FallbackAlbum, extract_domain (s));
    }
    else
    {
        // any other URI:
        // use the top two path elements as the artist and album

        if ((s = strstr (filepath, "://")))
        {
            s += 3;
            sep = '/';
        }
        else
        {
#ifdef _WIN32
            if (g_ascii_isalpha (filepath[0]) && filepath[1] == ':')
                s = filepath + 2;
            else
#endif
                s = filepath;

            sep = G_DIR_SEPARATOR;
        }

        StringBuf buf = str_copy (s);

        char * first = split_folder (buf, sep);
        char * second = (first && first > buf) ? split_folder (buf, sep) : nullptr;

        // skip common strings and avoid duplicates
        for (auto skip : (const char *[]) {"~", "music", artist, album, genre})
        {
            if (first && skip && ! strcmp_nocase (first, skip))
            {
                first = second;
                second = nullptr;
            }

            if (second && skip && ! strcmp_nocase (second, skip))
                second = nullptr;
        }

        if (first)
        {
            if (second && ! artist && ! album)
            {
                data->set_str (FallbackArtist, second);
                data->set_str (FallbackAlbum, first);
            }
            else
                data->set_str (artist ? FallbackAlbum : FallbackArtist, first);
        }
    }
}

EXPORT void Tuple::generate_title ()
{
    if (! data)
        return;

    auto title = get_str (Title);
    if (title)
        return;

    data = TupleData::copy_on_write (data);

    auto filepath = get_str (Path);
    if (filepath && ! strcmp (filepath, "cdda://"))
    {
        // audio CD:
        // use "Track N" as the title

        int subtune = get_int (Subtune);
        if (subtune >= 0)
            data->set_str (FallbackTitle, str_printf (_("Track %d"), subtune));
    }
    else if (filepath && ! strcmp (filepath, "dvd://"))
    {
        // DVD:
        // use "Track N" as the title

        int subtune = get_int (Subtune);
        if (subtune >= 0)
            data->set_str (FallbackTitle, str_printf (_("Title %d"), subtune));
    }
    else
    {
        auto filename = get_str (Basename);
        data->set_str (FallbackTitle, filename ? (const char *) filename : _("(unknown title)"));
    }
}

EXPORT void Tuple::delete_fallbacks ()
{
    if (! data)
        return;

    data = TupleData::copy_on_write (data);
    data->lookup (FallbackTitle, false, true);
    data->lookup (FallbackArtist, false, true);
    data->lookup (FallbackAlbum, false, true);
}

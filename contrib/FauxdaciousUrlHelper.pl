#!/usr/bin/perl

#MUST INSTALL youtube-dl FOR Youtube to work!
#pp -o FauxdaciousUrlHelper.exe -M Tunein::Streams -M IHeartRadio::Streams -M WWW::YouTube::Download -M utf8_heavy.pl -l libeay32_.dll -l zlib1_.dll -l ssleay32_.dll FauxdaciousUrlHelper.pl

#FAUXDACIOUS "HELPER" SCRIPT TO HANDLE URLS THAT FAUXDACIOUS CAN'T PLAY DIRECTLY:

#USAGE:  ~/.config/audacious[_instancename]/config:  [audacious].url_helper=FauxdaciousUrlHelper.pl

#THIS SCRIPT WORKS BY TAKING THE URL THAT FAUXDACIOUS IS ATTEMPTING TO ADD TO IT'S
#PLAYLIST AND TRIES TO MATCH IT AGAINST USER-WRITTEN REGEX PATTERNS.  IF IT MATCHES
#ONE THEN CODE IS EXECUTED TO CONVERT IT TO A PLAYABLE URL AND WRITTEN OUT AS A 
#TEMPORARY ONE (OR MORE) LINE ".pls" "PLAYLIST" THAT FAUXDACIOUS WILL THEN ACTUALLY
#ADD TO IT'S PLAYLIST.  THIS IS NECESSARY BECAUSE THERE'S NO OTHER KNOWN WAY TO 
#CHANGE THE URL WITHIN FAUXDACIOUS.  IF THE URL "FALLS THROUGH" - NOT MATCHING ANY
#OF THE USER-SUPPLIED PATTERNS HERE, THIS PROGRAM EXITS SILENTLY AND FAUXACIOUS 
#HANDLES THE URL NORMALLY (UNCHANGED).
#THE CURRENTLY-PROVIDED PATTERNS ARE FOR "tunein.com" RADIO STATIONS AND "youtube" 
#VIDEOS, FOR WHICH THE ACTUALLY VALID STREAM URL MUST BE LOCATED AND RETURNED.
#TEMPLATE FOR ADDING USER-DEFINED PATTERNS:
#if ($ARGV[0] =~ <pattern>) {
#    <logic to locate proper stream URL, and perhaps the tag metadata / cover art url>
#    $newPlaylistURL = <proper stream URL to actually play>;
#    $title = <default descriptive title tag text to describe the stream>; #OPTIONAL
#    $comment = <additional tag metadata for tag file>; #OPTIONAL
#         #$comment SHOULD EITHER BE EMPTY OR IN THE FORM:  "Comment=string\nArtist=string...\n" etc.
#         #ONE USE FOR comment IS TO FETCH A COVER-ART THUMBNAIL IMAGE FILE AND PASS 
#         #IT'S URI/URL HERE AS "Comment=<art_uri>" SEE THE PREDEFINED PATTERNS FOR EXAMPLES:
#    &writeTagData($comment);
#}
#YOU CAN CHANGE "DEFAULT" TO "OVERRIDE" IN writeTagData() TO KEEP STATION TITLE FROM BEING 
#OVERWRITTEN BY CURRENT SONG TITLE:

#===================================================================================================

#STRIP OUT INC PATHS USED IN COMPILATION - COMPILER PUTS EVERYTING IN IT'S OWN
#TEMPORARY PATH AND WE DONT WANT THE RUN-TIME PHISHING AROUND THE USER'S LOCAL
#MACHINE FOR (POSSIBLY OLDER) INSTALLED PERL LIBS (IF HE HAS PERL INSTALLED)!
BEGIN
{
	$isExe = 1  if ($0 =~ /exe$/io);
	if ($isExe)
	{
		while (@INC)
		{
			$_ = shift(@INC);
			push (@myNewINC, $_)  if (/(?:cache|CODE)/o);
		}
		@INC = @myNewINC;
	}
	else
	{
		while (@INC)   #REMOVE THE "." DIRECTORY!
		{
			$_ = shift(@INC);
			push (@myNewINC, $_)  unless ($_ eq '.');
		}
		@INC = @myNewINC;
	}
}
use strict;
use LWP::Simple qw();
use StreamFinder;
my $haveTuneinStreams = 1;
my $haveIheartRadioStreams = 1;
my $haveYoutubeDownload = 1;

die "..usage: $0 url\n"  unless ($ARGV[0]);
my $configPath = '';
if ($ARGV[1]) {
	($configPath = $ARGV[1]) =~ s#^file:\/\/##;
}
my $newPlaylistURL = '';
my $title = $ARGV[0];
my $comment = '';

#BEGIN USER-DEFINED PATTERN-MATCHING CODE:

#NAAAH - TURN OFF m3u PLUGIN TO LOAD FIRST TIME. - if ($ARGV[0] =~ /^http/ && $ARGV[0] =~ /\.m3u8?\b/i) {
#	$newPlaylistURL = $ARGV[0];  #FORCE FAUXDACIOUS TO HANDLE http://url.m3u[8] AS A STREAM, *NOT* A PLAYLIST!:
#} else {
	my $client = new StreamFinder($ARGV[0]);
	die "f:Could not open streamfinder or no streams found!"  unless ($client);

	$newPlaylistURL = $client->getURL();
	die "f:No streams for $ARGV[0]!"  unless ($newPlaylistURL);
	$title = $client->getTitle();
	my $art_url = $client->getIconURL();
	my $stationID = $client->getID();
	$comment = "Album=$ARGV[0]\n";
	$comment .= "Artist=".$client->{artist}."\n"  if (defined $client->{artist});
	if ($art_url) {
		my ($image_ext, $art_image) = $client->getIconData;
		if ($configPath && $art_image && open IMGOUT, ">${configPath}/${stationID}.$image_ext") {
			binmode IMGOUT;
			print IMGOUT $art_image;
			close IMGOUT;
			my $path = $configPath;
			if ($path =~ m#^\w\:#) { #WE'RE ON M$-WINDOWS, BUMMER: :(
				$path =~ s#^(\w)\:#\/$1\%3A#;
				$path =~ s#\\#\/#g;
			}
			$comment .= "Comment=file://${path}/${stationID}.$image_ext\n";
		}
	}
#}
&writeTagData($client, $comment);
#END USER-DEFINED PATTERN-MATCHING CODE.
exit (0)  unless ($newPlaylistURL);

if ($configPath) {
	die "f:Can't open ${configPath}/tempurl.pls for writing ($!)"
			unless (open PLAYLISTOUT, ">${configPath}/tempurl.pls");
} else {
	open PLAYLISTOUT, ">&STDOUT";
}
print PLAYLISTOUT <<EOF;
[playlist]
NumberOfEntries=1
File1=$newPlaylistURL
EOF

close PLAYLISTOUT;

exit(0);

sub writeTagData {
	my $client = shift;
	my $comment = shift || '';
	my @tagdata = ();
	# WE WRITE Facebook/Youtube VIDEOS TO A TEMP. TAG FILE, SINCE THEY EXPIRE AND ARE USUALLY ONE-OFFS, WHICH
	# WE THEREFORE WANT Fauxdacious TO DELETE THE TAGS AND COVER ART FILES WHEN PLAYLIST CLEARED (fauxdacious -D)!:
	my $tagfid = ($client && $client->getType() =~ /(?:Facebook|Youtube)/i) ? 'tmp_tag_data' : 'user_tag_data';
	if (open TAGDATA, "<${configPath}/$tagfid") {
		my $omit = 0;
		while (<TAGDATA>) {
			$omit = 1  if (/^\[$newPlaylistURL\]/);
			if ($omit) {
				$omit = 0  if (/^\[/o && !/$newPlaylistURL/);
			}
			push @tagdata, $_  unless ($omit);
		}
		close TAGDATA;
	}
	if (open TAGDATA, ">${configPath}/$tagfid") {
		while (@tagdata) {
			print TAGDATA shift(@tagdata);
		}
		# USER:CHANGE "DEFAULT" TO "OVERRIDE" BELOW TO KEEP STATION TITLE FROM BEING OVERWRITTEN BY CURRENT SONG TITLE:
		print TAGDATA <<EOF;
[$newPlaylistURL]
Precedence=DEFAULT
Title=$title
$comment
EOF

		close TAGDATA;
	}
}


__END__

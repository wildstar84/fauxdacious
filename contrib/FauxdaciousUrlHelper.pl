#!/usr/bin/perl

#MUST INSTALL youtube-dl FOR Youtube to work!
#pp -o FauxdaciousUrlHelper.exe -M Tunein::Streams -M IHeartRadio::Streams -M WWW::YouTube::Download -l libeay32_.dll -l zlib1_.dll -l ssleay32_.dll FauxdaciousUrlHelper.pl

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
my $haveTuneinStreams = 0;
my $haveIheartRadioStreams = 0;
my $haveYoutubeDownload = 0;
eval 'use Tunein::Streams; $haveTuneinStreams = 1; 1';
eval 'use IHeartRadio::Streams; $haveIheartRadioStreams = 1; 1';
eval 'use WWW::YouTube::Download; $haveYoutubeDownload = 1; 1';

die "..usage: $0 url\n"  unless ($ARGV[0]);
my $configPath = '';
if ($ARGV[1]) {
	($configPath = $ARGV[1]) =~ s#^file:\/\/##;
}
my $newPlaylistURL = '';
my $title = $ARGV[0];

#BEGIN USER-DEFINED PATTERN-MATCHING CODE:
if ($haveYoutubeDownload && ($ARGV[0] =~ m#^http[s]?\:\/\/\w*\.*youtu(?:be)?\.# 
		|| $ARGV[0] =~ m#^http[s]?\:\/\/\w*\.*vimeo\.#)) {   #HANDLE Youtube/Vimeo URLS:
	my $client = WWW::YouTube::Download->new;
	my $meta_data = '';
	eval "\$meta_data = \$client->prepare_download(\$ARGV[0]);";
	my %metadata;
	if ($meta_data) {
		foreach my $name (qw(video_id title user)) {
			eval "\$metadata{$name} = \$client->get_$name(\$ARGV[0]);";
		}
	}
	$title = $metadata{title};
	my $comment = "Album=$ARGV[0]\n";
	$comment .= "Artist=$metadata{user}\n"  if ($metadata{user});
	$_ = `youtube-dl --get-url --get-thumbnail -f mp4 $ARGV[0]`;
	my @urls = split(/\r?\n/);
	while (@urls && $urls[0] !~ m#\:\/\/#o) {
		shift @urls;
	}
	$newPlaylistURL = $urls[0];
	my $art_url = ($#urls >= 1) ? $urls[$#urls] : '';
	if ($configPath) {
		my $art_image = $art_url ? LWP::Simple::get($art_url) : '';
		if ($art_image) {
			(my $image_ext = $art_url) =~ s/^.+\.//;
			if (open IMGOUT, ">/tmp/$metadata{video_id}.$image_ext") {
				binmode IMGOUT;
				print IMGOUT $art_image;
				close IMGOUT;
				if ($configPath =~ m#^\w\:#) { #WE'RE ON M$-WINDOWS, BUMMER: :(
					$comment .= "Comment=file:///C%3A/tmp/$metadata{video_id}.$image_ext\n";
				} else {
					$comment .= "Comment=file:///tmp/$metadata{video_id}.$image_ext\n";
				}
			}
		}
	}
	chomp $newPlaylistURL;
	die "f:No valid video stream found!\n"  unless ($newPlaylistURL);
	&writeTagData($comment);
} elsif (($haveTuneinStreams && $ARGV[0] =~ m#\:\/\/tunein\.com\/#)
		|| ($haveIheartRadioStreams && $ARGV[0] =~ m#\:\/\/www\.iheart\.com\/#)) {   #HANDLE tunein.com & iheart.com STATION URLS:
	my $isTunein = ($ARGV[0] =~ /tunein\./) ? 1 : 0;
	my $station = $isTunein ? new Tunein::Streams($ARGV[0])
			: new IHeartRadio::Streams($ARGV[0], 'secure_shoutcast', 'secure', 'any', '!rtmp');
	die "f:Invalid URL($ARGV[0]) or no streams!\n"  unless ($station);
	$newPlaylistURL = $isTunein ? $station->getBest('Url') : $station->getStream();
	die "f:No streams for $ARGV[0]!\n"  unless ($newPlaylistURL);
	
	my $art_url = $station->getIconURL();
	my $stationID = $isTunein ? $station->getStationID() : $station->getFccID();
	$title = $station->getStationTitle();
	my $comment = "Album=$ARGV[0]\n";
	if ($art_url) {
		my $art_image = $art_url ? LWP::Simple::get($art_url) : '';
		my $image_ext = $art_url;
		if ($isTunein) {
			$image_ext =~ s/^.+\.//;
		} else {
			$image_ext = ($art_url =~ /\.(\w+)$/) ? $1 : 'png';
		}
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
	&writeTagData($comment);
}
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
	my $comment = shift || '';
	my @tagdata = ();
	if (open TAGDATA, "<${configPath}/user_tag_data") {
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
	if (open TAGDATA, ">${configPath}/user_tag_data") {
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

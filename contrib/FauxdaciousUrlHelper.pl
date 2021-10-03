#!/usr/bin/perl

#MUST INSTALL youtube-dl FOR Youtube to work!
#pp --gui -o FauxdaciousUrlHelper.exe -M urlhelper_mods.pm -M utf8_heavy.pl -l libeay32_.dll -l zlib1_.dll -l ssleay32_.dll FauxdaciousUrlHelper.pl

###(urlhelper_modules.pm contains):
###use StreamFinder;
###use StreamFinder::_Class;
###use StreamFinder::Anystream;
###use StreamFinder::Apple;
###use StreamFinder::Bitchute;
###use StreamFinder::Blogger;
###use StreamFinder::Brighteon;
###use StreamFinder::Castbox;
###use StreamFinder::Google;
###use StreamFinder::IHeartRadio;
###use StreamFinder::RadioNet;
###use StreamFinder::Rumble;
###use StreamFinder::SermonAudio;
###use StreamFinder::Spreaker;
###use StreamFinder::Tunein;
###use StreamFinder::Vimeo;
###use StreamFinder::Youtube;
###1;

#FAUXDACIOUS "HELPER" SCRIPT TO HANDLE URLS THAT FAUXDACIOUS CAN'T PLAY DIRECTLY:

#USAGE:  $0 URL [download-path]

#NOTE:  SEE ALSO THE CONFIG (.ini) FILE: FauxdaciousUrlHelper.ini in /contrib/, 
#       MOVE OVER TO ~/.config/fauxdacious[_instancename]/ AND EDIT TO SUIT YOUR NEEDS!

#CONFIGURE:  ~/.config/fauxdacious[_instancename]/config:  [audacious].url_helper=FauxdaciousUrlHelper.pl

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
use StreamFinder;

#THESE SERVERS WILL TIMEOUT ON YOU TRYING TO STREAM, SO DOWNLOAD TO TEMP. FILE, THEN PLAY INSTEAD!:
#FORMAT:  '//www.problemserver.com' [, ...]
my @downloadServerList = ();  #USER MAY ADD ANY SUCH SERVERS TO THE LIST HERE OR IN FauxdaciousUrlHelper.ini.

die "..usage: $0 URL [download-path]\n"  unless ($ARGV[0]);
exit (0)  if ($ARGV[0] =~ m#^https?\:\/\/r\d+\-\-#);  #DON'T REFETCH FETCHED YOUTUBE PLAYABLE URLS!

my $configPath = '';
if ($ARGV[1]) {
	($configPath = $ARGV[1]) =~ s#^file:\/\/##;
}
my $newPlaylistURL = '';
my $title = $ARGV[0];
my $comment = '';
my $client = 0;
my $downloadit = 0;
my $DEBUG = defined($ENV{'FAUXDACIOUS_DEBUG'}) ? $ENV{'FAUXDACIOUS_DEBUG'} : 0;

	if ($configPath && open IN, "<${configPath}/FauxdaciousUrlHelper.ini") {
		while (<IN>) {
			s/\r//go;
			chomp;
			next  if (/^\#/o);
			next  unless (/\S/o);
			s/^\s+//o;
			s/\,$//o;
			s/^[\'\"]//o;
			s/[\'\"]$//o;
			push @downloadServerList, $_;
		}
		close IN;
	}

#BEGIN USER-DEFINED PATTERN-MATCHING CODE:

	if ($ARGV[0] !~ /\.(?:mp3|mpv|m3u|m3u8|webm|pls|mov|mp[4acdp]|m4a|avi|flac|flv|og[agmvx]|wav|rtmp|3gp|a[ac]3|ape|dts|tta)$/i) {  #ONLY FETCH STREAMS FOR URLS THAT DON'T ALREADY HAVE VALID EXTENSION!
		$client = new StreamFinder($ARGV[0], -debug => $DEBUG,
				-log => '/tmp/FauxdaciousUrlHelper.log',
				-logfmt => '[time] "[title]" - [site]: [url] ([total])'
		);
		die "f:Could not open streamfinder or no streams found!"  unless ($client);

		$newPlaylistURL = $client->getURL('-nopls');
		die "f:No streams for $ARGV[0]!"  unless ($newPlaylistURL);

		$title = $client->getTitle();
		my $art_url = $client->getIconURL();
		my $desc = $client->getTitle('desc');

		#NOTE:  MOST PODCAST EPISODE mp3's SEEM TO COME WITH THEIR OWN EMBEDDED ICONS OR RELY ON THE ALBUM'S ICON:
		#(SO OPTIONS #1 & #2 ARE PBLY POINTLESS (CREATE USELESS REDUNDANT ICON FILES):
#1		(my $stationID = $client->getID()) =~ s#\/.*$##;  #0: THIS NAMES IMAGE AFTER STATION-ID.
		(my $stationID = $client->getID()) =~ s#\/#\_#;   #1: THIS NAMES IMAGE AFTER STATION-ID[_PODCAST-ID].
#2		my $stationID = $client->getID();                 #2: THIS KEEPS SEPARATE PODCAST-ID icon in SEPARATE SUBDIRECTORY, CREATING IF NEEDED:
#2		if ($stationID =~ m#^([^\/]+)\/#) {
#2			my $substationDIR = $1;
#2			`mkdir ${configPath}/${substationDIR}`  unless (-d "${configPath}/${substationDIR}");
#2		}

		$comment = 'Album=' . ((defined($client->{album}) && $client->{album} =~ /\S/) ? ($client->{album}." - $ARGV[0]") : $ARGV[0]) . "\n";
		$comment .= 'Artist='.$client->{artist}."\n"  if (defined($client->{artist}) && $client->{artist} =~ /\w/);
		$comment .= 'AlbumArtist='.$client->{albumartist}."\n"  if (defined($client->{albumartist}) && $client->{albumartist} =~ /\w/);
		$comment .= 'Year='.$client->{year}."\n"  if (defined($client->{year}) && $client->{year} =~ /\d\d\d\d/);
		$comment .= 'Genre='.$client->{genre}."\n"  if (defined($client->{genre}) && $client->{genre} =~ /\w/);
		#PUT DESCRIPTION FIELD, IF ANY INTO THE "Lyrics" FOR DISPLAY IN LYRICS PLUGINS.
		#NOTE:  IF THE STREAM HAS LYRICS IN id3 TAGS, THEY WILL REPLACE THIS, REGARDLESSOF "Precedence".
		#COMMENT OUT NEXT 7 LINES IF THIS IS NOT DESIRED:
		if (defined($desc) && $desc =~ /\w/ && $desc ne $title) {
			$desc =~ s/\R+$//s;
			$desc =~ s/\R/\x02/gs; #MUST BE A SINGLE LINE, SO WORKAROUND FOR PRESERVING MULTILINE DESCRIPTIONS!
			$desc =~ s#(p|br)\>\s*\x02\s*\<(p|br)#$1\>\<$2#ig;  #TRY TO REMOVE SOME TRIPLE-SPACING.
			my $lyrics = "Lyrics=Description:  $desc";
			$comment .= "$lyrics\n";
		}
		if ($art_url) {
			my ($image_ext, $art_image) = $client->getIconData;
			$image_ext =~ tr/A-Z/a-z/;
			if ($configPath && $art_image && open IMGOUT, ">${configPath}/${stationID}.$image_ext") {
				binmode IMGOUT;
				print IMGOUT $art_image;
				close IMGOUT;
				my $path = $configPath;
				if ($path =~ m#^\w\:#) { #WE'RE ON M$-WINDOWS, BUMMER: :(
					$path =~ s#^(\w)\:#\/$1\%3A#;
					$path =~ s#\\#\/#g;
				}
				$comment .= "Comment=file://${path}/${stationID}.$image_ext";
				my $art_url2 = $client->getIconURL('artist');
				if ($art_url2 && $art_url2 ne $art_url) {
					my ($image_ext, $art_image) = $client->getIconData('artist');
					$image_ext =~ tr/A-Z/a-z/;
					if ($configPath && $art_image && open IMGOUT, ">${configPath}/${stationID}_channel.$image_ext") {
						binmode IMGOUT;
						print IMGOUT $art_image;
						close IMGOUT;
						my $path = $configPath;
						if ($path =~ m#^\w\:#) { #WE'RE ON M$-WINDOWS, BUMMER: :(
							$path =~ s#^(\w)\:#\/$1\%3A#;
							$path =~ s#\\#\/#g;
						}
						$comment .= ";file://${path}/${stationID}_channel.$image_ext";
					}
				}
				$comment .= "\n";
			}
		}
		$comment =~ s/\0/ /gs;          #NO NULLS ALLOWED!
		$comment =~ s/[\x80-\xff]//gs;  #MUST ALSO STRIP OUT ALL NON-ASCII CHARACTERS!
	} else {
		$newPlaylistURL = $ARGV[0];
	}

	foreach my $s (@downloadServerList) {
		if ($newPlaylistURL =~ /\Q$s\E/) {
			$downloadit = 1;
			last;
		}
	}
	if ($downloadit) {  #SOME SERVERS HANG UP IF TRYING TO STREAM, SO DOWNLOAD TO TEMP. FILE INSTEAD!:
		my $fn = $1  if ($newPlaylistURL =~ m#([^\/]+)$#);
		exit (0)  unless ($fn);
		unless (-f "/tmp/$fn") {
			my $cmd = "curl -o /tmp/$fn \"$newPlaylistURL\"";
			`$cmd`;
		}
		if (-f "/tmp/$fn") {
			$newPlaylistURL = "file:///tmp/$fn";
			my $getTitle = `ffprobe -loglevel error -show_entries format_tags=title -of default=noprint_wrappers=1:nokey=1 /tmp/$fn`;
			if ($getTitle =~ /\w/) {
				chomp $getTitle;
				$title = $getTitle;
			}
		} else {
			exit (0);
		}
	}

    &writeTagData($client, $comment, $downloadit);

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
	my $downloadit = shift || 0;
	my @tagdata = ();
	# WE WRITE VIDEOS/PODCASTS TO A TEMP. TAG FILE, SINCE THEY EXPIRE AND ARE USUALLY ONE-OFFS, WHICH
	# WE THEREFORE WANT Fauxdacious TO DELETE THE TAGS AND COVER ART FILES WHEN PLAYLIST CLEARED (fauxdacious -D)!
	# THE LIST IN THE REGEX BELOW ARE THE ONES TO *NOT* DELETE ART IMAGES FOR (ie. STREAMING STATIONS)!:
	my $tagfid = (!$downloadit && $client && $client->getType()
			=~ /^(?:IHeartRadio|RadioNet|Radionomy|Reciva|Tunein)$/)
			? 'user_tag_data' : 'tmp_tag_data';
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

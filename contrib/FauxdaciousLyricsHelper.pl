#!/usr/bin/perl

### HOW TO COMPILE TO EXE (FOR M$-WINDOWS USERS WITHOUT PERL INSTALLED (NEARLY ALL)):
###pp --gui -o FauxdaciousLyricsHelper.exe -M utf8_heavy.pl -M lyrichelper_modules -l libeay32_.dll -l zlib1_.dll -l ssleay32_.dll FauxdaciousLyricsHelper.pl

###(lyrichelper_modules.pm contains):
###use LyricFinder;
###use LyricFinder::ApiLyricsOph;
###use LyricFinder::AZLyrics;
###use LyricFinder::Cache;
###use LyricFinder::Genius;
###use LyricFinder::Musixmatch;
###1;

#FAUXDACIOUS "HELPER" SCRIPT TO FETCH Lyrics FROM sites supported by the LyricFinder CPAN module:

#USAGE:  $0 artist title [configpath] [album [flags]]

#NOTE:  SEE ALSO THE CONFIG (.ini) FILE: FauxdaciousLyricsHelper.ini in /contrib/, 
#       MOVE OVER TO ~/.config/fauxdacious[_instancename]/ AND EDIT TO SUIT YOUR NEEDS!

#CONFIGURE:  ~/.config/fauxdacious[_instancename]/config:  [audacious].lyrics_helper=FauxdaciousLyricsHelper.pl

#THIS SCRIPT ATTEMPTS TO FETCH THE LYRICS FOR THE CURRENT SONG PLAYING IN FAUXDACIOUS FOR THE NEW
#"PURE PERL" VERSIONS OF THE LYRICWIKI PLUGINS USING A VERIETY OF LYRICS SITES (SUPPORTD BY PERL'S
#LyricFinder MODULE).  SITE CHANGES ARE NOW MAINTAINED THERE.  THIS REPLACES THE HARDCODED
#USE OF THE SINGLE SITE: https://lyrics.fandom.com/ (NOW DEFUNCT) FROM AUDACIOUS.
#SUPPORTED LYRICS SITES ARE TRIED IN RANDOM ORDER TO REDUCE EXCESS ACTIVITY ON ANY ONE SITE.
#NEW SITES CAN BE ADDED BY UPDATES TO LyricFinder RATHER THAN HAVING TO UPDATE C CODE AND
#REBUILDNG FAUXDACIOUS!

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
use warnings;
use LyricFinder;

#USER: EDIT ~/.config/fauxdacious[_instancename]/FauxdaciousLyricsHelper.txt TO
#ADD STREAMING STATIONS THAT SWITCH BACK TO THEIR STATION TITLE NEAR END OF SONG BEFORE SWITCHING TO
#NEXT SONG IN ORDER TO AVOID RE-SEARCHING THE LYRICS SITES FOR LIKELY NON-EXISTANT "TITLE" EVERY SONG!
#FORMAT:  artist name|title name (one pair per line).

#YOU CAN ALSO ADD A USER-AGENT LINE TO SEND TO THE LYRICS SITES HERE:
#SEE LyricFinder DOCS FOR THE DEFAULT USER-AGENT VALUE:
#(CURRENTLY:  "Mozilla/5.0 (X11; Linux x86_64; rv:80.0) Gecko/20100101 Firefox/80.0").
#FORMAT:  agent="user-agent string"
my @SKIPTHESE = ();
my @SKIPALBUMS = ();
my $agent = '';

my $DEBUG = defined($ENV{'FAUXDACIOUS_DEBUG'}) ? $ENV{'FAUXDACIOUS_DEBUG'} : 0;

print STDERR "-LyricHelper(".join('|',@ARGV)."=\n"  if ($DEBUG);
unlink "$ARGV[2]/_albumart_done.tmp"	if ($ARGV[2] && -d $ARGV[2] && -e "$ARGV[2]/_albumart_done.tmp");

my $flags = '';
if ($#ARGV >= 1) {
	$flags = $ARGV[4]  if (defined $ARGV[4]);
	if ($ARGV[2] && -d $ARGV[2]) {
		## USER-CONFIGURED SITE-SKIP LIST:
		if (open IN, "<$ARGV[2]/FauxdaciousLyricsHelper.ini") {
			while (<IN>) {
				s/\r//go;
				chomp;
				next  if (/^\#/o);
				next  unless (/\S/o);
				s/^\s+//o;
				s/\,$//o;
				if (s/^agent\s*\=\s*[\'\"]?//o) {
					($agent = $_) =~ s/[\'\"]$//;
					next;
				}
				s/^[\'\"]//o;
				s/[\'\"]$//o;
				if (/\|/o) {
					push @SKIPTHESE, $_;
				} else {
					push @SKIPALBUMS, $_;
				}
			}
			close IN;
		}
	}

	for (my $a=0;$a<=$#ARGV;$a++) {  #STRIP QUOTES AROUND ARGUMENTS OFF (M$-Windows EXE)!:
		$ARGV[$a] =~ s/^[\'\"]//;
		$ARGV[$a] =~ s/[\'\"]$//;
	}

	unlink "$ARGV[2]/_tmp_lyrics.txt"  if ($ARGV[2] && -d $ARGV[2] && -f "$ARGV[2]/_tmp_lyrics.txt");
	print STDERR "..LYRICS HELPER: Args=".join('|', @ARGV)."=\n"  if ($DEBUG);

	#SKIP ANY ALBUMS (STATIONS) THE USER DOESN'T WANT LYRICS FETCHED FOR:
	if ($#ARGV >= 3) {
		foreach my $skipit (@SKIPALBUMS) {
			print STDERR "-???- ALB=$ARGV[3]= SKIPIT=$skipit=\n"  if ($DEBUG > 1);
			if ("$ARGV[3]" =~ /^\Q${skipit}\E$/i) {
				print STDERR "i:LYRICS HELPER: SKIPPING ALBUM ($skipit) AS CONFIGURED.\n"  if ($DEBUG);
				exit (0);  #QUIT
			}
		}
	}

	#SKIP ANY SONGS THE USER DOESN'T WANT LYRICS FETCHED FOR:
	foreach my $skipit (@SKIPTHESE) {
		print STDERR "-???- AT=$ARGV[0]|$ARGV[1]= SKIPIT=$skipit=\n"  if ($DEBUG > 1);
		if ("$ARGV[0]|$ARGV[1]" =~ /^\Q${skipit}\E$/i) {
			print STDERR "i:LYRICS HELPER: SKIPPING ($skipit) AS CONFIGURED.\n"  if ($DEBUG);
			exit (0);  #QUIT
		}
	}

	my $omit = '';
	if ($flags =~ /ALBUMART/ && -d $ARGV[2]) {  #ALBUMART PLUG IS ALSO ACTIVE & SET TO PULL FROM WEB.
		#WAIT FOR 5" & AND SEE IF ALBUMART ALREADY PULLED LYRICS WHILST PULLING AN ALBUM-COVER:
		#(THIS STRATEGY IS USED TO AVOID HITTING A LYRIC SITE TWICE, SINCE BOTH ALBUM-ART AND
		#LYRICWIKI SHARE SOME SITES (CURRENTLY:  Genius.com and Musixmatch.com, AS THESE CAN
		#PROVIDE BOTH COVER-ART IMAGES AND LYRICS!:
		my $timeout = 6;
		our $quit = 0;

		local $SIG{ALRM} = sub { print STDERR "--QUIT!--\n"; $quit = 1; };
		alarm $timeout;

		my $checklyrics = 0;
		while (1) {  #WAIT 5 SECONDS TO GIVE ALBUMART A CHANCE TO FETCH THE LYRICS FOR US:
			last  if ($quit);
			if (-e "$ARGV[2]/_albumart_done.tmp") {
				if (open IN, "$ARGV[2]/_albumart_done.tmp") {
					$omit = <IN>;  #CONTAINS A STRING OF LYRICS SITES ALBUMART ALREADY TRIED (IF ANY):
					close IN;
					$omit = ''  unless (defined $omit);
				}
				unlink "$ARGV[2]/_albumart_done.tmp";
				$checklyrics = 1;
				last;
			}
			sleep (1);
		}
		alarm 0;

		#IF ALBUMART COMPLETED WITHIN THE ALLOWED TIME & SAVED SOME LYRICS FOR US, USE THEM!:
		if ($checklyrics && -e "$ARGV[2]/_tmp_lyrics_from_albumart.txt") {
			unlink("$ARGV[2]/_tmp_lyrics.txt")  if (-e "$ARGV[2]/_tmp_lyrics.txt");
			rename "$ARGV[2]/_tmp_lyrics_from_albumart.txt", "$ARGV[2]/_tmp_lyrics.txt";
			exit (0);
		}
	}

	my $lf = $omit ? new LyricFinder(-omit => $omit) : new LyricFinder();
	if ($lf) {
		$lf->agent($agent)  if ($agent);
		my $lyrics = $lf->fetch($ARGV[0], $ARGV[1], 'random');
		if (defined($lyrics) && $lyrics) {
			my $doschar = ($^O =~ /Win/) ? "\r" : '';
			$lyrics .= "${doschar}\n(Lyrics courtesy: ".$lf->source().")${doschar}\n".$lf->site();
			if ($ARGV[2] && -d $ARGV[2] && open OUT, ">$ARGV[2]/_tmp_lyrics.txt") {
				binmode OUT;
				print OUT $lyrics;  #MUST NULL-TERMINATE FOR FAUXDACIOUS'S C CODE!
				close OUT;
			} else {
				print STDERR $lyrics;
			}
			unlink("$ARGV[2]/_tmp_lyrics_from_albumart.txt")  if (-d $ARGV[2] && -e "$ARGV[2]/_tmp_lyrics_from_albumart.txt");
			exit (0);
		}
		else
		{
			print STDERR "w:No lyrics found (last tried=".$lf->url()."): ".$lf->message()."\n";
		}
	}
	else
	{
		print STDERR "s:Could not find/load required LyricFinder module!\n";
	}
} else {
	print STDERR "..usage: $0 artist title [output_directory [album [flags]]]\n";
}
unlink("$ARGV[2]/_tmp_lyrics_from_albumart.txt")  if (-d $ARGV[2] && -e "$ARGV[2]/_tmp_lyrics_from_albumart.txt");

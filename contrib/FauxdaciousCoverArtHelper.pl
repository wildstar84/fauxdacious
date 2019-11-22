#!/usr/bin/perl -w

#pp -o FauxdaciousCoverArtHelper.exe -M utf8_heavy.pl -l libeay32_.dll -l zlib1_.dll -l ssleay32_.dll FauxdaciousCoverArtHelper.pl

#FAUXDACIOUS "HELPER" SCRIPT TO FETCH COVER ART FOR CDs/DVSs FROM coverartarchive and dvdcover.com:

#USAGE:  $0 {CD[T] diskID | DVD title} [configpath] | DELETE COVERART configpath

#CONFIGURE:  ~/.config/audacious[_instancename]/config:  [audacious].cover_helper=FauxdaciousCoverArtHelper.pl

#THIS SCRIPT ATTEMPTS TO FETCH THE COVER ART FOR CDs AND DVDs AND, IF NEEDED, THE TRACK TITLES AND ARTISTS 
#FOR CDs.  IT ALSO HANDLES DELETING TEMPORARY COVER-ART IMAGES FOR FAUXDACIOUS.
#IT CURRENTLY SCRAPES SITES:  dvdcover.com, archive.org AND/OR musicbrainz.org FOR THE NEEDED INFORMATION.
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
use HTML::Entities ();
#use LWP::Simple qw();
use LWP::UserAgent ();
my $haveCurl = 0;

die "..usage: $0 {CD[T] diskID | DVD title} [configpath] | DELETE COVERART configpath\n"  unless ($ARGV[0] && $ARGV[1]);
my $configPath = '';
if ($ARGV[1]) {
	($configPath = $ARGV[2]) =~ s#^file:\/\/##;
}

my $html = '';
my $title = '';
my $artist = '';
my $comment = '';
my $DEBUG=1;
my $ua = LWP::UserAgent->new;		
$ua->timeout(10);
$ua->cookie_jar({});
$ua->env_proxy;
my $bummer = ($^O =~ /MSWin/);
if ($bummer && $configPath) {  #STUPID WINDOWS DOESN'T SHOW DEBUG OUTPUT ON TERMINAL!
	my $homedir = $ENV{'HOMEDRIVE'} . $ENV{'HOMEPATH'};
	$homedir ||= $ENV{'LOGDIR'}  if ($ENV{'LOGDIR'});
	$homedir =~ s#[\/\\]$##;
	my $log_fh;
    open $log_fh, ">${configPath}/FauxdaciousCoverArtHelper_log.txt";
    *STDERR = $log_fh;
}
print STDERR "-FaudHelper(".join('|',@ARGV)."=\n"  if ($DEBUG);
if ($ARGV[0] =~ /^DELETE/ && $ARGV[1] =~ /^COVERART/ && $configPath) {  #WE'RE REMOVING ALL OLD COVERART IMAGE FILES:
	if (open TAGDATA, "<${configPath}/tmp_tag_data") {
		my $fid;
		my $skipdisks = 0;
		while (<TAGDATA>) {
			if (m#^\[([^\]]+)#o) {
				my $one = $1;
				$skipdisks = ($one =~ m#^(?:cdda|dvd)\:\/\/#o) ? 1 : 0;
			} elsif (!$skipdisks && m#^Comment\=file\:\/\/\/(.+)#o) {  #THIS ELIMINATES ANY EXISTING CD TAGS (WILL ALL BE REPLACED!):
				$fid = $1;
				next  if ($fid =~ /[\*\?]/o);  #GUARD AGAINST WILDCARDS, ETC!!
				next  unless ($fid =~ /(?:png|jpe?g|gif)$/io);  #MAKE SURE WE ONLY DELETE IMAGE FILES!
				$fid =~ s/^(\w)\%3A/$1\:/;
				if ($bummer && $fid =~ /^\w\:/) {   #BUMMER, WE'RE ON WINDOWS:
					$fid =~ s#\/#\\#go;  #MAY NOT BE NECESSARY?
				} else {
					$fid = '/' . $fid  unless ($fid =~ m#^\/#o);
				}
				print STDERR "i:$0: DELETED temp. image file ($fid)!\n";
				unlink $fid;
			}
		}
		close TAGDATA;
	}	
} elsif ($ARGV[0] =~ /^CD/i) {  #WE'RE AN AUDIO-CD: LOOK UP DISK-ID ON Musicbrainz.com:
	print STDERR "-0(CD): URL=https://musicbrainz.org/otherlookup/freedbid?other-lookup.freedbid=$ARGV[1]\n"  if ($DEBUG);
	my $response = $ua->get("https://musicbrainz.org/otherlookup/freedbid?other-lookup.freedbid=$ARGV[1]");
	if ($response->is_success) {
		$html = $response->decoded_content;
	} else {
		print STDERR $response->status_line  if ($DEBUG);
	}
	die "f:failed to find title ($ARGV[1]) on musicbrainz.com!\n"  unless ($html);
	my $url2 = '';
	my ($tmpurl2, $tmptitle, $tmpartist);
	while ($html)  #MAY BE MULTIPLE MATCHES, TRY TO FIND ONE THAT'S A "CD" (VS. A "DVD", ETC.):
	{
		$html =~ s#^.*?\<tr[^\>]*\>##is;
		if ($html =~ s#^.*?\<a\s+href\=\"([^\"]+)\"\>\s*\<bdi\>([^\<]+)\<\/bdi\>##is) {
			$tmpurl2 = $1;
			$tmptitle = $2;
			if ($html =~ s#^.*?\<bdi\>([^\<]+)\<\/bdi\>##is)
			{
				$tmpartist = $1;
				print STDERR "-1a: artist=$tmpartist= url=$tmpurl2= title=$tmptitle=\n"  if ($DEBUG);
				$html =~ s#^.*?\<td\>##is;
				if ($html =~ s#^CD\<\/td\>##is) {   #FOUND A DEFINITE "CD" (GRAB & QUIT!):
					$url2 = $tmpurl2;   #URL OF PAGE CONTAINING DETAILS FOR THE PARTICULAR CD:
					$title = HTML::Entities::decode_entities($tmptitle);
					$artist = HTML::Entities::decode_entities($tmpartist);
					print STDERR "-1a: CD url=$url2= title=$title= artist=$artist=\n"  if ($DEBUG);
					last;
				} elsif ($html =~ s#^([^C]+CD|\(unknown\))\<\/td\>##is) {  #FOUND WHAT MIGHT BE A CD, KEEP LOOKING:
					$url2 = $tmpurl2;
					$title = HTML::Entities::decode_entities($tmptitle);
					$artist = HTML::Entities::decode_entities($tmpartist);
				} else {    #SKIP THIS ROW:
					$html =~ s#^[^\<]*\<\/td\>##is;
				}
				next;
			}
		}
		else
		{
			last;
		}
	}
	if ($url2) {   #NOW FETCH THE DETAILS (COVER ART & TRACK TITLES) FOR THE MATCHED CD:
		my @tracktitle = ();
		my @trackartist = ();
		my $art_url = '';
		$url2 = 'https://musicbrainz.org' . $url2  unless ($url2 =~ /^http/);
		print STDERR "-2: URL2=$url2=\n"  if ($DEBUG);
		die "f:failed to parse out link on musicbrainz.com!\n"  unless ($url2);
		$html = '';
		my $response = $ua->get($url2);
		if ($response->is_success) {
			$html = $response->decoded_content;
		} else {
			print STDERR $response->status_line  if ($DEBUG);
		}
		print STDERR "-3: html=$html=\n"  if ($DEBUG > 1);
		die "f:musicbrainz.org returned no html!\n"  unless ($html);
		#TRY TO FIND THE COVER-ART THUMBNAIL URL:
		if ($html =~ s/data\-small\-thumbnail\=\"([^\"]+)\"//) {
			$art_url = $1;
		} elsif ($html =~ s/data\-fallback\=\"([^\"]+)\"//) {
			$art_url = $1;
		} elsif ($html =~ s/\<div\s+class\=\"cover\-art\"\>\<img\s+src\=\"([^\"]+)\"//) {
			$art_url = $1;
		}
		$art_url = 'http:' . $art_url  if ($art_url =~ m#^\/\/#);
		print STDERR "-4: cover url=$art_url=\n"  if ($DEBUG);
		my $trk = 0;
		if ($ARGV[0] =~ /^CDT/i) {  #WE NEED TRACK TITLES ALSO, FIND & GRAB 'EM ALL:
			$html =~ s#\<span class\=\"name\-variation\"\>##gs;
			while ($html =~ s#\<a\s+href\=\"https\:\/\/musicbrainz\.org\/track\/[^\"]+\"\>(\d+)\<\/a\>\s+\<\/td\>[^\<]+\<td\>\<[^\<]+\<bdi>([^\<]+)\<\/bdi\>##) {
				my $t = HTML::Entities::decode_entities($2);  #WE MUST DE-HTML & MAKE ALL-ASCII FOR Fauxdacious:
				print STDERR "--track$trk($1)=$t=\n"  if ($DEBUG);   #WE MAKE ASCII BY REMOVING ALL ACCENTS!:
				$t =~ tr/ŠšžŸÀÁÂÃÅÄÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖÙÚÛÜÝàáâãäåçèéêëìíîïðñòóôõöùúüûýø×°/SszYAAAAAACEEEEIIIIDNOOOOOUUUUYaaaaaaceeeeiiiionooooouuuuy0x /;
				$t =~ s/[\x80-\xff]//g;
				$tracktitle[$trk] = $t;
				if ($html =~ s#\<td\>\s*\<a\s+href\=\"\/artist\/[^\"]*\" title\=\"[^\"]*\"\>\<bdi\>([^\<]*)\<\/bdi\>##s) {
					my $t = HTML::Entities::decode_entities($1);
					$t =~ tr/ŠšžŸÀÁÂÃÅÄÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖÙÚÛÜÝàáâãäåçèéêëìíîïðñòóôõöùúüûýø×°/SszYAAAAAACEEEEIIIIDNOOOOOUUUUYaaaaaaceeeeiiiionooooouuuuy0x /;
					$t =~ s/[\x80-\xff]//g;
					$trackartist[$trk] = $t;
				}
				$trackartist[$trk] ||= $artist;
				print STDERR "-4.1: artist($trk)=$trackartist[$trk]=\n"  if ($DEBUG);
				++$trk;
			}
		} else {
			die "f:No cover art url found!\n"  unless ($art_url);
		}
		
		$comment = "Album=$title\n";
		$comment .= "AlbumArtist=$artist\n"  if ($artist);
		#x$comment .= "Artist=$artist\n"  if ($artist);
		if ($art_url) {   #WE FOUND AN IMAGE LINK TO WHAT SHOULD BE "COVER-ART":
			my $image_ext = ($art_url =~ /\.(\w+)$/) ? $1 : 'jpg';
			my $art_image;
			print STDERR "-5: ext=$image_ext= art_url=$art_url= configpath=$configPath=\n"  if ($DEBUG);
			if ($configPath && -e "${configPath}/$ARGV[1].$image_ext") {  #IF WE ALREADY HAVE THIS IMAGE, DON'T RE-DOWNLOAD IT!:
				print STDERR "-5a: already have art image (${configPath}/$ARGV[1].$image_ext)!\n"  if ($DEBUG);
				my $path = $configPath;
				if ($bummer && $path =~ m#^\w\:#) { #WE'RE ON M$-WINDOWS, BUMMER: :(
					$path =~ s#^(\w)\:#\/$1\%3A#;
					$path =~ s#\\#\/#g;
				}
				$comment .= "Comment=file://${path}/$ARGV[1].$image_ext\n";
			} else {   #WE DON'T HAVE IT, SO DOWNLOAD IT:
				print STDERR "-5b: attempt to download art image (${configPath}/$ARGV[1].$image_ext)!\n"  if ($DEBUG);
				my $response = $ua->get($art_url);
				if ($response->is_success) {
					$art_image = $response->decoded_content;
				} else {
					print STDERR $response->status_line  if ($DEBUG);
				}
				#NOW WRITE OUT THE DOWNLOADED IMAGE TO A FILE Fauxdacious CAN FIND:
				if ($configPath && $art_image && open IMGOUT, ">${configPath}/$ARGV[1].$image_ext") {
					print STDERR "-6: will write art image to (${configPath}/$ARGV[1].$image_ext)\n"  if ($DEBUG);
					binmode IMGOUT;
					print IMGOUT $art_image;
					close IMGOUT;
					my $path = $configPath;
					if ($bummer && $path =~ m#^\w\:#) { #WE'RE ON M$-WINDOWS, BUMMER: :(
						$path =~ s#^(\w)\:#\/$1\%3A#;
						$path =~ s#\\#\/#g;
					}
					$comment .= "Comment=file://${path}/$ARGV[1].$image_ext\n";
				}
			}
		}
		&writeTagData($comment, \@tracktitle, \@trackartist);  #WRITE OUT ALL THE TRACK TAGS TO user_tag_data AND CUSTOM (<disk-id>.tag):
	}
} elsif ($ARGV[0] =~ /^DVD/i) {   #WE'RE A DVD:  JUST LOOK UP AND DOWNLOAD THE COVER-ART:
	foreach my $image_ext (qw(jpg png jpeg gif)) {
		if ($configPath && -e "${configPath}/$ARGV[1].$image_ext") {
			print STDERR "-DVD: done:Cover art file (${configPath}/$ARGV[1].$image_ext) found locally.\n"  if ($DEBUG);
			exit (0);
		}
	}
	eval "use LWP::Curl; \$haveCurl = 1; 1";   #MUST USE CURL ON Dvdcover.com - THEY CHECK FOR AD-BLOCKERZ:
	(my $argv1clean = $ARGV[1]) =~ s/[\x00-\x1f]//g;
	my $argv1unclean = $argv1clean;
	my $art_url = '';

	if ($haveCurl || !$bummer) {   #FIRST, TRY TO FETCH FROM Dvdcover.com:
		print STDERR "-1a: URL=https://dvdcover.com/?s=$argv1clean=\n"  if ($DEBUG);
		my $lwpcurl = LWP::Curl->new(timeout => 10)  if ($haveCurl);
		goto PLAN_B  if ($bummer && !$lwpcurl);
OUTER:		for (my $try=0;$try<=1;$try++) {
			$html = '';
			print STDERR "-1a TRY($try) of 2: argv=$argv1clean=\n"  if ($DEBUG);
			$html = $lwpcurl->get("https://dvdcover.com/?s=$argv1clean")  if ($lwpcurl);
			print STDERR "--falling back to WGET (wget -t 2 -T 20 -O- -o /dev/null \"https://dvdcover.com/?s=$argv1clean\" 2>/dev/null)!...\n"  if ($DEBUG && !$html);
			$html ||= `wget -t 2 -T 20 -O- -o /dev/null \"https://dvdcover.com/?s=$argv1clean\" 2>/dev/null `  unless ($bummer);
##			if ($html =~ s#\<img\s+src\=\"(https\:\/\/dvdcover\.com\/wp\-content\/uploads\/$argv1clean\S*?\-front\-www\.getdvdcovers\.com\_\-[0-9x]+\.jpg)##is) {
			if ($html =~ m#^.+\<ul\s+id\=\"infinite\-articles\"(.+)?\<\/ul\>#s) {
				my $html0 = $1;
				$html0 =~ s#\<\/ul\>.+##s;
				my $imghtml = '';
				$html = $html0;
				my $entryhtml = '';
				#MATCHES DO NOT EXACTLY MATCH BASED ON DVD_TITLE FROM libdvdread, SO WE HAVE TO SEARCH/GUESS BEST ONE!:
				while ($html =~ s#(\<li\s+.+?\<\/li\>)##so) {  # 1ST CHECK IF DESCRIPTION MATCHES TITLE & IS DVD COVER:
					$entryhtml = $1;
					if ($entryhtml =~ /\bdescription\=\"?([^\"]+)/i) {
						my $desc = $1;
						(my $title = $argv1clean) =~ s/\_/ /g;
						print STDERR "-TRY 1- DESC=$desc= TITLE=$title=\n";
						if ($desc =~ /$title/i && $desc =~ /DVD Cover/i) {
							if ($entryhtml =~ m#\<img\s+([^\>]+)\>#i) {
								my $imghtml = $1;
								$art_url = $1  if ($imghtml =~ m#src\=\"([^\"]+)#);
								print STDERR "---GOT IT1! ($art_url)\n";
								last   if ($art_url);
							}
						}
					}
				}
				unless ($art_url) {
					$html = $html0;
					while ($html =~ s#(\<li\s+.+?\<\/li\>)##so) { # 2ND CHECK IF TAGGED AS DVD COVER:
						$entryhtml = $1;
						if ($entryhtml =~ /\<p\>\<p\>([^\<]+)/i) {
							my $tagshtml = $1;
							print STDERR "-TRY 2- TAGS=$tagshtml=\n";
							if ($tagshtml =~ /DVD Cover/i) {
								if ($entryhtml =~ m#\<img\s+([^\>]+)\>#i) {
									my $imghtml = $1;
									$art_url = $1  if ($imghtml =~ m#src\=\"([^\"]+)#);
									print STDERR "---GOT IT2! ($art_url)\n";
									last   if ($art_url);
								}
							}
						}
					}
				}
				unless ($art_url) {  # 3RD CHECK IF IMAGE URL ITSELF CONTAINS THE TITLE
					$html = $html0;
					while ($html =~ s#(\<li\s+.+?\<\/li\>)##so) {
						$entryhtml = $1;
						if ($entryhtml =~ m#\<img\s+([^\>]+)\>#i) {
							my $imghtml = $1;
							if ($imghtml =~ /$argv1clean/i) {
								$art_url = $1  if ($imghtml =~ m#src\=\"([^\"]+)#);
								print STDERR "---GOT IT3! ($art_url)\n";
								last   if ($art_url);
							}
						}
					}
				}
				unless ($art_url) { # LAST CHECK IF THE REFERENCE LINK CONTAINS THE TITLE:
					(my $key = $argv1clean) =~ s/\_/\-/g;
					$html = $html0;
					while ($html =~ s#(\<li\s+.+?\<\/li\>)##so) {
						$entryhtml = $1;
						if ($entryhtml =~ m#\<a\s+href\=\"([^\"]+)\"\>\s+\<img\s+([^\>]+)\>#i) {
							my $link = $1;
							my $imghtml = $2;
							print STDERR "-TRY 4- KEY=$key= link=$link=\n";
							if ($link =~ m#\/$key#i) {
								$art_url = $1  if ($imghtml =~ m#src\=\"([^\"]+)#);
								print STDERR "---GOT IT4! ($art_url)\n";
								last   if ($art_url);
							}
						}
					}
				}
			}
			$argv1clean =~ s/\_\d+x.*$//i;  #TRY AGAIN W/O EXTRA DISK OR SIZE INFO IN TITLE:
			$argv1clean =~ s/\_DISC.*$//;
			last  if ($argv1clean eq $argv1unclean);
		}
		if ($art_url) {   #GOT AN IMAGE LINK TO (HOPEFULLY THE RIGHT) COVER-ART IMAGE:
			my $image_ext = ($art_url =~ /\.(\w+)$/) ? $1 : 'jpg';
			my $art_image = '';
			print STDERR "-5: ext=$image_ext= art_url=$art_url= configpath=$configPath=\n"  if ($DEBUG);
			print STDERR "-5b: attempt to download art image (${configPath}/$ARGV[1].$image_ext)!\n"  if ($DEBUG);
			my $response = $ua->get($art_url);
			$art_image = $lwpcurl->get($art_url,'https://dvdcover.com')  if ($lwpcurl);
			$art_image ||= `wget -t 2 -T 20 -O- -o /dev/null --referer=\"https://dvdcover.com\" \"$art_url\" 2>/dev/null `  unless ($bummer);
			#NOW WRITE OUT THE DOWNLOADED IMAGE TO A FILE Fauxdacious CAN FIND:
			if ($configPath && $art_image && open IMGOUT, ">${configPath}/$ARGV[1].$image_ext") {
				print STDERR "-6: will write art image to (${configPath}/$ARGV[1].$image_ext)\n"  if ($DEBUG);
				binmode IMGOUT;
				print IMGOUT $art_image;
				close IMGOUT;
			}
			exit (0);
		}
	}
PLAN_B:   #PLAN "B":  NOT ON Dvdcover.com, SO LET'S TRY Archive.org (Dvdcover easier & has better DVD images, but I think Archive.org has more):
	print STDERR "-1(PLAN B): URL=https://archive.org/details/coverartarchive?and[]=$argv1clean=\n"  if ($DEBUG);
	$argv1clean = $argv1unclean;
	for (my $try=0;$try<=1;$try++) {
		print STDERR "-1a TRY($try) of 2: argv=$argv1clean=\n"  if ($DEBUG);
		my $response = $ua->get("https://archive.org/details/coverartarchive?and[]=$argv1clean");
		if ($response->is_success) {
			$html = $response->decoded_content;
		} else {
			print STDERR $response->status_line  if ($DEBUG);
		}
		print STDERR "-1a TRY($try) of 2: HTML=$html=\n"  if ($DEBUG > 1);
		if ($html && $html =~ s#\<a\s+href\=\"\/details\/mbid\-([^\"]+)\"\s+title\=\"([^\"]+)\"##is) {
			my $mbid = $1;
			$title = $2;
			print STDERR "-2a: mbid=$mbid= title=$title=\n"  if ($DEBUG);
			if ($html =~ s#\<img\s+class\=\"item\-img\s*\"\s+source\=\"(\/services\/img\/mbid\-$mbid)\"##is) {
				$art_url = 'https://archive.org' . $1;
				print STDERR "-3: cover url=$art_url=\n"  if ($DEBUG);
			}
			last;
		}
		$argv1clean =~ s/\_\d+x.*$//;  #TRY AGAIN W/O EXTRA DISK OR SIZE INFO IN TITLE:
		$argv1clean =~ s/\_DISC.*$//;
		last  if ($argv1clean eq $argv1unclean);
	}
	print STDERR "-4: title=$title= cover url=$art_url=\n"  if ($DEBUG);
	die "f:No cover art url found!\n"  unless ($art_url);

	if ($art_url) {   #GOT AN IMAGE LINK TO (HOPEFULLY THE RIGHT) COVER-ART IMAGE:
		my $image_ext = ($art_url =~ /\.(\w+)$/) ? $1 : 'jpg';
		my $art_image = '';
		print STDERR "-5: ext=$image_ext= art_url=$art_url= configpath=$configPath=\n"  if ($DEBUG);
		print STDERR "-5b: attempt to download art image (${configPath}/$ARGV[1].$image_ext)!\n"  if ($DEBUG);
		my $response = $ua->get($art_url);
		if ($response->is_success) {
			$art_image = $response->decoded_content;
		} else {
			print STDERR $response->status_line  if ($DEBUG);
		}
		#NOW WRITE OUT THE DOWNLOADED IMAGE TO A FILE Fauxdacious CAN FIND:
		if ($configPath && $art_image && open IMGOUT, ">${configPath}/$ARGV[1].$image_ext") {
			print STDERR "-6: will write art image to (${configPath}/$ARGV[1].$image_ext)\n"  if ($DEBUG);
			binmode IMGOUT;
			print IMGOUT $art_image;
			close IMGOUT;
		}
	}
}
else
{
	die "..usage: $0 {CD diskID | DVD title} [configpath]\n"
}

exit(0);

sub writeTagData {   #FOR CDs:  WRITE ALL TAG-DATA FOUND FOR EACH TRACK TO user_tag_data AND, IF NEEDED, CUSTOM TAG-FILE (<disk-ID>.tag)
	my $comment = shift || '';
	my $tracktitles = shift || '';
	my $trackartists = shift || '';
	my @tagdata = ();
	(my $nocomment = $comment) =~ s/Comment\=\S*//gs;  #CON'T INCLUDE COVER-ART FILE IN CUSTOM-FILE COMMENTS (REDUNDANT!)
	chomp $nocomment;
	my $trktitle = ($#{$tracktitles} >= 0) ? shift (@{$tracktitles}) : $title;
	my $trkartist = ($#{$trackartists} >= 0) ? shift (@{$trackartists}) : $artist;
	$trkartist = "Artist=$trkartist\n"  if ($trkartist =~ /\w/o);
	print STDERR "-7: comment=$comment= tagfid=${configPath}/tmp_tag_data=\n"  if ($DEBUG);
	if (open TAGDATA, "<${configPath}/tmp_tag_data") {  #MUST FIRST FETCH EXISTING user_tag_data SINCE WE'LL OVERWRITE IT:
		my $omit = 0;
		while (<TAGDATA>) {
			$omit = 1  if (/^\[cdda\:/);  #THIS ELIMINATES ANY EXISTING CD TAGS (WILL ALL BE REPLACED!):
			if ($omit) {                  #(CD TAGS ARE NOT TIED TO A SPECIFIC CD)
				$omit = 0  if (/^\[/o && !/cdda\:/);
			}
			push @tagdata, $_  unless ($omit);
		}
		close TAGDATA;
	}
	#NOW REPLACE user_tag_data APPENDING OUR CURRENT CD TAGS:
	my $customAlso = (($ARGV[0] =~ /^CDT/i) && open CUSTOM, ">${configPath}/$ARGV[1].tag");
	if (open TAGDATA, ">${configPath}/tmp_tag_data") {
		while (@tagdata) {
			print TAGDATA shift(@tagdata);
		}
		# USER:CHANGE "DEFAULT" TO "OVERRIDE" BELOW TO KEEP STATION TITLE FROM BEING OVERWRITTEN BY CURRENT SONG TITLE:
		my $trk = 1;
		while (1) {
			print TAGDATA <<EOF;
[cdda://?$trk]
Precedence=OVERRIDE
Title=$trktitle
${trkartist}Track=${trk}
$comment
EOF

			#THE PURPOSE OF THE CUSTOM TAG FILE IS TO PREVENT REPEATED WEB LOOKUPS OF DATA EVERY TIME CD IS PLAYED!:
			if ($customAlso) {
				print CUSTOM <<EOF;
[cdda://?$trk]
Precedence=OVERRIDE
Title=$trktitle
${trkartist}Track=${trk}
$nocomment
EOF

			}
			last  unless (@{$tracktitles});
			$trktitle = shift (@{$tracktitles});
			$trkartist = shift (@{$trackartists});
			$trkartist = "Artist=$trkartist\n"  if ($trkartist =~ /\w/o);
			++$trk;
		}
		close CUSTOM  if ($customAlso);
		close TAGDATA;
	}
}

__END__

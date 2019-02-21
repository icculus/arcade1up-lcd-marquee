#!/usr/bin/perl -w

# arcade1up-lcd-marquee; control an LCD in a Arcade1Up marquee.
#
# Please see the file LICENSE.txt in the source's root directory.
#
#  This file written by Ryan C. Gordon.

use warnings;
use strict;
use XML::LibXML;

my $debug = 0;

sub quit {
    my $rc = shift;
    sleep(5) if $debug;
    exit($rc);
}

sub usage {
    print STDERR "USAGE: $0 <SYSTEM> <EMULATOR> <ROM> <COMMAND>\n";
    quit(1);
}

my $system;
my $emulator;
my $rom;
my $cmd;
my $cmdline_ok = 0;
foreach (@ARGV) {
    $debug = 1, next if ($_ eq '--debug');
    $system = $_, next if (not defined $system);
    $emulator = $_, next if (not defined $emulator);
    $rom  = $_, next if (not defined $rom);
    $cmd = $_, $cmdline_ok = 1, next if (not defined $cmd);
    usage();
}

usage() if not $cmdline_ok;

print("Starting up!\n") if $debug;

if ($debug) {
    print("system: '$system'\n");
    print("emulator: '$emulator'\n");
    print("rom: '$rom'\n");
    print("command: '$cmd'\n");
    print("\n");
}


# Okay, so we're going to dig into the system's game list that
#  EmulationStation uses, and see what metadata is available for it. We'll
#  try to find a useful image in there. We find the game by looking for one
#  with the same "rom" specified. It's not a perfect system, but it'll do.

# !!! FIXME: this works for emulators but not "ports" like sdlpop.
my $sysdir = "/home/pi/RetroPie/roms/$system";
my $gamelist = "$sysdir/gamelist.xml";

if ( ! -f $gamelist ) {
    print("There's no gamelist.xml; nothing to do...\n") if $debug;
    quit(0);
}

$rom = `realpath "$rom"`;

my $xml = XML::LibXML->load_xml(location => $gamelist);
foreach my $game ($xml->findnodes('/gameList/game')) {
    my $path = $game->findvalue('./path');
    next if not $path;
    if (not $path =~ /\A\//) {
        $path = `realpath "$sysdir/$path"`;
    }

    next if $path ne $rom;

    my $name = $game->findvalue('./name');
    $name = '[title unknown]' if not $name;
    print("Found it! It appears to be \"$name\"\n") if $debug;

    my $img = $game->findvalue('./marquee');
    $img = $game->findvalue('./image') if not $img;

    if ($img) {
        if (not $img =~ /\A\//) {
            $img = `realpath "$sysdir/$img"`;
        }
    }

    if (not $img) {
        # !!! FIXME: maybe a system image (like an Apple for basilisk?) if still not found?
        print("Found game, but no usable image for it.  :(\n") if $debug;
    } else {
        $img =~ s/\n//g;
        print("Going with image '$img' for the marquee!\n") if $debug;
        system("/home/pi/projects/arcade1up-lcd-marquee/marquee-showimage $img");
    }

    last;
}

print("All done!\n") if $debug;

quit(0);

# end of runcommand_onstart.sh ...


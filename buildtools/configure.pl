#!/usr/bin/perl -w

require 5.000;

use strict;
use English;
use File::Basename;
use Cwd 'abs_path';
use Fcntl;
use Config;
#use File::Temp "tempfile";   Not available before Perl 5.6.1
    

my ($TRUE, $FALSE) = (1,0);

# This program generates config.mk, which is included by all of the
# Netpbm makefiles.  You run this program as the first step in building 
# Netpbm.  (The second step is 'make').

# This program is only a convenience.  It is supported to create 
# config.mk any way you want.  In fact, an easy way is to copy
# config.mk.in and follow the instructions in the comments therein
# to uncomment certain lines and make other changes.

# Note that if you invoke 'make' without having first run 'configure',
# the make will call 'configure' itself when it finds
# 'config.mk' missing.  That might look a little messy to the
# user, but it isn't the normal build process.

# The argument to this program is the filepath of the config.mk.in
# file.  If unspecified, the default is 'config.mk.in' in the 
# Netpbm source directory.

# For explanations of the stuff we put in the make files, see the comments
# in config.mk.in.


# $testCc is the command we use to do test compiles.  Note that test
# compiles are never more than heuristics, because we may be configuring
# a build that will happen on a whole different system, which will build
# programs to run on a third system.

my $testCc;

# $warned is logical.  It means we have issued a warning to Standard Output.

my $warned;


##############################################################################
#
#  Implementation note:
#
#  At one time, we thought we had to add /usr/local/lib and /usr/local/include
#  to the path on some platforms because they needed it and didn't include
#  it in the default compiler search path.  But then we had reason to doubt
#  that was really required, so removed the function on 04.03.15 and never
#  had any complaint in the next 3 years.
##############################################################################


#******************************************************************************
#
#  SUBROUTINES
#
#*****************************************************************************

sub autoFlushStdout() {
    my $oldFh = select(STDOUT);
    $OUTPUT_AUTOFLUSH = $TRUE;
    select($oldFh);
}



sub prompt($$) {

    my ($prompt, $default) = @_;

    my $defaultPrompt = defined($default) ? $default : "?";

    print("$prompt [$defaultPrompt] ==> ");

    my $response = <STDIN>;

    if (defined($response)) {
        chomp($response);
        if ($response eq "" && defined($default)) {
            $response = $default;
        }
    } else {
        print("\n");
        die("End of file on Standard Input when expecting response to prompt");
    }

    return $response;
}



sub promptYesNo($) {
    my ($default) = @_;

    my $retval;

    while (!defined($retval)) {
        my $response = prompt("(y)es or (n)o", $default);
        
        if (uc($response) =~ m{ ^ (Y|YES) $ }x)  {
            $retval = $TRUE;
        } elsif (uc($response) =~ m{ ^ (N|NO) $ }x)  {
            $retval = $FALSE;
        } else {
            print("'$response' isn't one of the choices.  \n" .
                  "You must choose 'yes' or 'no' (or 'y' or 'n').\n");
        }
    }

    return $retval;
}



sub flow($) {
    my ($unflowed) = @_;
#-----------------------------------------------------------------------------
#  Return the text $unflowed, split with newlines into 72 character lines.
#  We assum $unflowed is pure text, without any kind of formatting characters
#  such as newlines.
#-----------------------------------------------------------------------------
    my $retval;

    my @words = split(m{\s+}, $unflowed);
    
    my $currentLine;
    
    $currentLine = "";
    
    foreach my $word (@words) {
        my $mustSpill;
        if (length($currentLine) == 0) {
            $currentLine = $word;
            $mustSpill = $FALSE;
        } else {
            my $separator;
            if (substr($currentLine, -1) eq '.') {
                $separator = "  ";
            } else {
                $separator = " ";
            }
                
            if (length($currentLine) + length($separator) + length($word)
                <= 72) {
                $currentLine .= $separator;
                $currentLine .= $word;
                $mustSpill = $FALSE;
            } else {
                $mustSpill = $TRUE;
            }
        }
        if ($mustSpill) {
            $retval .= $currentLine;
            $retval .= "\n";
            $currentLine = $word;
        }
    }

    $retval .= $currentLine;
    $retval .= "\n";

    return $retval;
}



sub warnUser($) {
    my ($warning) = @_;
#-----------------------------------------------------------------------------
#  Warn the user the build might fail, with explanation "$warning".
#-----------------------------------------------------------------------------
    print("************************************" .
          "************************************\n");
    print flow("WARNING: $warning");
    print("************************************" .
          "************************************\n");

    $warned = $TRUE;
}



sub tmpdir() {
# This is our approximation of File::Spec->tmpdir(), which became part of
# basic Perl some time after Perl 5.005_03.

    my $retval;
    
    if ($ENV{'TMPDIR'}) {
        $retval = $ENV{'TMPDIR'};
    } elsif ($ENV{'TEMP'}) {
        $retval = $ENV{'TEMP'};
    } else {
        if ($Config{'osvers'} eq 'djgpp') {
            $retval = '/dev/env/DJDIR/tmp';
        } else {
            if (-d('/tmp')) {
                $retval =  '/tmp';
            }
        }
    }
    return $retval;
}



sub tempFile($) {

# Here's what we'd do if we could expect Perl 5.6.1 or later, instead
# of calling this subroutine:
#    my ($cFile, $cFileName) = tempfile("netpbmXXXX", 
#                                       SUFFIX=>".c", 
#                                       DIR=>File::Spec->tmpdir(),
#                                       UNLINK=>0);
    my ($suffix) = @_;

    my $fileName;
    local *file;  # For some inexplicable reason, must be local, not my

    my $tmpdir = tmpdir();

    if (!defined($tmpdir)) {
        print STDERR ("Cannot determine what directory to use for " .
                      "temporary files.  " .
                      "Set TMPDIR environment variable to fix this.\n");
        exit(1);
    } else {
        if (!-d($tmpdir)) {
            print STDERR ("Temporary file directory '$tmpdir' does not " .
                          "exist.  Create it or set TMPDIR environment " .
                          "variable appropriately\n");
            exit(1);
        } else {
            for (my $i = 0; $i < 50 && !defined($fileName); ++$i) {
                my $trialFileName = tmpdir() . "/netpbm" . $i . $suffix;

                my $success = sysopen(*file,
                                      $trialFileName,
                                      O_RDWR|O_CREAT|O_EXCL);

                if ($success) {
                    $fileName = $trialFileName;
                }
            }

            if (!defined($fileName)) {
                print STDERR ("Unable to create a temporary file in " .
                              "directory '$tmpdir'\n");
                exit(1);
            }
        }
    }
    return(*file, $fileName);
}



sub commandExists($) {
    my ($command) = @_;
#-----------------------------------------------------------------------------
#  Return TRUE iff a shell command $command exists.
#-----------------------------------------------------------------------------

# Note that it's significant that the redirection on the following
# causes it to be executed in a shell.  That makes the return code
# from system() a lot different than if system() were to try to
# execute the program directly.

    return(system("$command 1</dev/null 1>/dev/null 2>/dev/null")/256 != 127); 
}



sub chooseTestCompiler($$) {

    my ($compiler, $testCcR) = @_;

    my $cc;

    if ($ENV{'CC'}) {
        $cc = $ENV{'CC'};
    } else {
        if (commandExists('cc')) {
            $cc = 'cc';
        } elsif (commandExists("gcc")) {
            $cc = 'gcc';
        }
    }
    $$testCcR = $cc;
}



sub testCflags() {

    my $cflags;

    $cflags = "";  # initial value 
    
    if ($ENV{"CPPFLAGS"}) {
        $cflags = $ENV{"CPPFLAGS"};
    } else {
        $cflags = "";
    }
    
    if ($ENV{"CFLAGS"}) {
        $cflags .= " " . $ENV{"CFLAGS"};
    }
    
    return $cflags;
}    



sub testCompile($$$) {
    my ($cflags, $cSourceCodeR, $successR) = @_;
#-----------------------------------------------------------------------------
#  Do a test compile of the program in @{$cSourceCodeR}.
#  
#  Return $$successR == $TRUE iff the compile succeeds (exit code 0).
#-----------------------------------------------------------------------------
    my ($cFile, $cFileName) = tempFile(".c");

    print $cFile @{$cSourceCodeR};
    
    my ($oFile, $oFileName) = tempFile(".o");
    # Note: we tried using /dev/null for the output file and got complaints
    # from the Sun compiler that it has the wrong suffix.  2002.08.09.
    
    my $compileCommand = "$testCc -c -o $oFileName $cflags $cFileName";
    print ("Doing test compile: $compileCommand\n");
    my $rc = system($compileCommand);
    
    unlink($oFileName);
    close($oFile);
    unlink($cFileName);
    close($cFile);

    $$successR = ($rc == 0);
}



sub testCompileLink($$$) {
    my ($flags, $cSourceCodeR, $successR) = @_;
#-----------------------------------------------------------------------------
#  Do a test compile and link of the program in @{$cSourceCodeR}.
#  
#  Return $$successR == $TRUE iff the compile succeeds (exit code 0).
#-----------------------------------------------------------------------------
    my ($cFile, $cFileName) = tempFile('.c');

    print $cFile @{$cSourceCodeR};
    
    my ($oFile, $oFileName) = tempFile('');

    # Note that $flags may contain -l options, which where static linking
    # is involved have to go _after_ the base object file ($oFileName).
    
    my $compileCommand = "$testCc -o $oFileName $cFileName $flags";
    print ("Doing test compile/link: $compileCommand\n");
    my $rc = system($compileCommand);
    
    unlink($oFileName);
    close($oFile);
    unlink($cFileName);
    close($cFile);

    $$successR = ($rc == 0);
}



sub displayIntroduction() {
    print("This is the Netpbm configurator.  It is an interactive dialog " .
          "that\n");
    print("helps you build the file 'config.mk' and prepare to build ");
    print("Netpbm.\n");
    print("\n");

    print("Do not be put off by all the questions.  Configure gives you " .
          "the \n");
    print("opportunity to make a lot of choices, but you don't have to.  " .
          "If \n");
    print("you don't have reason to believe you're smarter than Configure,\n");
    print("just take the defaults (hit ENTER) and don't sweat it.\n");
    print("\n");

    print("If you are considering having a program feed answers to the " .
          "questions\n");
    print("below, please read doc/INSTALL, because that's probably the " .
          "wrong thing to do.\n");
    print("\n");

    print("Hit ENTER to begin.\n");
    my $response = <STDIN>;
}


sub askAboutCygwin() {

    print("Are you building in/for the Cygwin environment?\n");
    print("\n");
    
    my $default;
    if ($OSNAME eq "cygwin") {
        $default = "y";
    } else {
        $default = "n";
    }
    
    my $retval = promptYesNo($default);

    return $retval;
}



sub askAboutDjgpp() {

    print("Are you building in/for the DJGPP environment?\n");
    print("\n");
    
    my $default;
    if ($OSNAME eq "dos") {
        $default = "y";
    } else {
        $default = "n";
    }
    
    my $retval = promptYesNo($default);
}



sub computePlatformDefault($) {

    my ($defaultP) = @_;

    if ($OSNAME eq "linux") {
        $$defaultP = "gnu";
    } elsif ($OSNAME eq "cygwin") {
        $$defaultP = "win";
    } elsif ($OSNAME eq "dos") {
        # DJGPP says "dos"
        $$defaultP = "win";
    } elsif ($OSNAME eq "aix" || $OSNAME eq "freebsd" || $OSNAME eq "darwin" ||
             $OSNAME eq "amigaos") {
        $$defaultP = $OSNAME;
    } elsif ($OSNAME eq "solaris") {
        $$defaultP = "sun";
    } elsif ($OSNAME eq "dec_osf") {
        $$defaultP = "tru64";
    } else {
        print("Unrecognized OSNAME='$OSNAME'.  No default possible\n");
    }
    # OK - if you know what $OSNAME is on any other platform, send me a patch!
}



sub getPlatform() {

    my $platform;
    my $default;

    computePlatformDefault(\$default);

    print("Which of the following best describes your platform?\n");
 
    print("gnu      GNU/Linux\n");
    print("win      Windows/DOS (Cygwin, DJGPP, Mingw32)\n");
    print("sun      Solaris or SunOS\n");
    print("hp       HP-UX\n");
    print("aix      AIX\n");
    print("tru64    Tru64\n");
    print("irix     Irix\n");
    print("bsd      NetBSD, BSD/OS\n");
    print("openbsd  OpenBSD\n");
    print("freebsd  FreeBSD\n");
    print("darwin   Darwin or Mac OS X\n");
    print("amigaos  Amiga\n");
    print("unixware Unixware\n");
    print("sco      SCO OpenServer\n");
    print("beos     BeOS\n");
    print("none     none of these are even close\n");
    print("\n");

    my $response = prompt("Platform", $default);

    my %platform = ("gnu"      => "GNU",
                    "sun"      => "SOLARIS",
                    "hp"       => "HP-UX",
                    "aix"      => "AIX",
                    "tru64"    => "TRU64",
                    "irix"     => "IRIX",
                    "win"      => "WINDOWS",
                    "beos"     => "BEOS",
                    "bsd"      => "NETBSD",
                    "openbsd"  => "OPENBSD",
                    "freebsd"  => "FREEBSD",
                    "unixware" => "UNIXWARE",
                    "sco"      => "SCO",
                    "darwin"   => "DARWIN",
                    "amigaos"  => "AMIGA",
                    "none"     => "NONE"
                    );

    $platform = $platform{$response};
    if (!defined($platform)) {
        print("'$response' isn't one of the choices.\n");
        exit 8;
    }

    my $subplatform;

    if ($platform eq "WINDOWS") {
        my ($djgpp, $cygwin);

        if ($OSNAME eq "dos") {
            $djgpp = askAboutDjgpp();
            if ($djgpp) {
                $cygwin = $FALSE;
            } else {
                $cygwin = askAboutCygwin();
            }
        } else {
            $cygwin = askAboutCygwin();
            if ($cygwin) {
                $djgpp = $FALSE;
            } else {
                $djgpp = askAboutDjgpp();
            }
        }

        if ($cygwin) {
            $subplatform = "cygwin";
        } elsif ($djgpp) {
            $subplatform = "djgpp";
        } else {
            $subplatform = "other";
        }
    }

    return($platform, $subplatform);
}



sub getGccChoiceFromUser($) {
    my ($platform) = @_;

    my $retval;

    print("GNU compiler or native operating system compiler (cc)?\n");
    print("\n");

    my $default;

    if ($platform eq "SOLARIS" || $platform eq "SCO" ) {
        $default = "gcc";
    } else {
        $default = "cc";
    }

    while (!defined($retval)) {
        my $response = prompt("gcc or cc", $default);

        if ($response eq "gcc") {
            $retval = "gcc";
        } elsif ($response eq "cc") {
            $retval = "cc";
        } else {
            print("'$response' isn't one of the choices.  \n" .
                  "You must choose 'gcc' or 'cc'.\n");
        }
    }
    if ($retval eq 'gcc' && !commandExists('gcc')) {
        warnUser("WARNING: You selected the GNU compiler, " .
                 "but do not appear to have a program " .
                 "named 'gcc' in your PATH.  This may " .
                 "cause trouble later.  You may need to " .
                 "set the CC environment variable or CC " .
                 "makefile variable or install 'gcc'");
    }
    print("\n");

    return $retval;
}



sub getCompiler($$$) {
    my ($platform, $subplatform, $compilerR) = @_;
#-----------------------------------------------------------------------------
#  Here are some of the issues surrounding choosing a compiler:
#
#  - It's not just the name of the program we need -- different compilers
#    need different options.
#
#  - There are basically two choices on any system:  native compiler or
#    GNU compiler.  That's all this program recognizes, anyway.  On some,
#    native _is_ GNU, and we return 'gcc'.
#
#  - A user may well have various compilers.  Different releases, using
#    different standard libraries, for different target machines, etc.
#
#  - The CC environment variable tells the default compiler.
#
#  - In the absence of a CC environment variable, 'cc' is the default
#    compiler.
#
#  - The user must be able to specify the compiler by overriding the CC
#    make variable (e.g. make CC=gcc2).
#
#  - Configure needs to do test compiles.  The test is best if it uses
#    the same compiler that the build eventually will use, but it's 
#    useful even if not.
#
# The value this subroutine returns is NOT the command name to invoke the
# compiler.  It is simply "cc" to mean native compiler or "gcc" to mean
# GNU compiler.
#-----------------------------------------------------------------------------
    my %gccOptionalPlatform = ("SOLARIS" => 1,
                               "TRU64"   => 1,
                               "SCO"     => 1,
                               "AIX"     => 1,
                               "HP-UX"   => 1);

    my %gccUsualPlatform = ("GNU"     => 1,
                            "NETBSD"  => 1,
                            "OPENBSD" => 1,
                            "FREEBSD" => 1,
                            "DARWIN"  => 1,
                            );

    if (commandExists('x86_64-w64-mingw32-gcc')) {
        printf("Do you want to use the Mingw-w64 Cross-Compiler?\n");

        if (promptYesNo('y') eq "y") {
            $$compilerR = 'x86_64-w64-mingw32-gcc';
        }
    }
    if (!defined($$compilerR)) {
        if ($gccUsualPlatform{$platform}) {
            $$compilerR = "gcc";
        } elsif ($platform eq "WINDOWS" && $subplatform eq "cygwin") {
            $$compilerR = "gcc";
        } elsif ($gccOptionalPlatform{$platform}) {
            $$compilerR = getGccChoiceFromUser($platform);
        } else {
            $$compilerR = 'cc';
        }
    }
}



sub gccLinker() {
#-----------------------------------------------------------------------------
#  Determine what linker Gcc on this system appears to use.
#
#  Return either "gnu" or "sun"
#
#  For now, we assume it must be either a GNU linker or Sun linker and
#  that all Sun linkers are fungible.
#
#  If we can't tell, we assume it is the GNU linker.
#-----------------------------------------------------------------------------
    # First, we assume that the compiler calls 'collect2' as the linker
    # front end.  The specs file might specify some other program, but
    # it usually says 'collect2'.
    
    my $retval;

    my $collect2 = qx{gcc --print-prog-name=collect2};
    
    if (defined($collect2)) {
        chomp($collect2);
        my $linker = qx{$collect2 -v 2>&1};
        if (defined($linker) && $linker =~ m{GNU ld}) {
            $retval = "gnu";
        } else {
            $retval = "sun";
        }
    } else {
        $retval = "gnu";
    }
    return $retval;
}



sub getLinker($$$$) {

    my ($platform, $compiler, $baseLinkerR, $viaCompilerR) = @_;

    my $baseLinker;

    if ($platform eq "SOLARIS") {
        $$viaCompilerR = $TRUE;

        while (!defined($$baseLinkerR)) {
            print("GNU linker or SUN linker?\n");
            print("\n");

            my $default;
            
            if ($compiler eq "gcc") {
                $default = gccLinker();
            } else {
                $default = "sun";
            }
            my $response = prompt("sun or gnu", $default);
            
            if ($response eq "gnu") {
                $$baseLinkerR = "GNU";
            } elsif ($response eq "sun") {
                $$baseLinkerR = "SUN";
            } else {
                print("'$response' isn't one of the choices.  \n" .
                      "You must choose 'sun' or 'gnu'.\n");
            }
            print("\n");
        }
    } else {
        $$viaCompilerR = $TRUE;
        $$baseLinkerR = "?";
    }
}



sub libSuffix($) {
    my ($platform) = @_;
#-----------------------------------------------------------------------------
#  Return the traditional suffix for the link-time library on platform
#  type $platform.
#
#  Note that this information is used mainly for cosmetic purposes in this
#  this program and the Netpbm build, because the build typically removes
#  any suffix and uses link options such as "-ltiff" to link the library.
#  This leaves it up to the linker to supply the actual suffix.
#-----------------------------------------------------------------------------
    my $suffix;

    if ($platform eq 'WINDOWS') {
        $suffix = '.dll';
    } elsif ($platform eq 'AIX') {
        $suffix = '.a';
    } elsif ($platform eq 'DARWIN') {
        $suffix = '.dylib';
    } else {
        $suffix = '.so';
    }
}



sub getLibTypes($$$$$$$$) {
    my ($platform, $subplatform, $default_target,
        $netpbmlibtypeR, $netpbmlibsuffixR, $shlibprefixlistR,
        $willBuildSharedR, $staticlib_tooR) = @_;

    print("Do you want libnetpbm to be statically linked or shared?\n");
    print("\n");

    my $default = ($default_target eq "merge") ? "static" : "shared";

    my ($netpbmlibtype, $netpbmlibsuffix, $shlibprefixlist, 
        $willBuildShared, $staticlib_too);
    
    my $response = prompt("static or shared", $default);
    
    if ($response eq "shared") {
        $willBuildShared = $TRUE;
        if ($platform eq "WINDOWS") {
            $netpbmlibtype = "dll";
            $netpbmlibsuffix = "dll";
            if ($subplatform eq "cygwin") {
                $shlibprefixlist = "cyg lib";
            } 
        } elsif ($platform eq "DARWIN") {
            $netpbmlibtype = "dylib";
            $netpbmlibsuffix = "dylib";
        } else {
	    if ($platform eq "IRIX") {
		$netpbmlibtype = "irixshared";
	    } else {
		$netpbmlibtype = "unixshared";
	    }
            if ($platform eq "AIX") {
                $netpbmlibsuffix = "a";
            } elsif ($platform eq "HP-UX") {
                $netpbmlibsuffix = "sl";
            } else {
                $netpbmlibsuffix = "so";
            }
        }
    } elsif ($response eq "static") {
        $willBuildShared = $FALSE;
        $netpbmlibtype = "unixstatic";
        $netpbmlibsuffix = "a";
        # targets, but needed for building
        # libopt 
    } else {
        print("'$response' isn't one of the choices.  \n" .
              "You must choose 'static' or 'shared'.\n");
        exit 12;
    }

    print("\n");

    # Note that we can't do both a static and shared library for AIX, because
    # they both have the same name: libnetpbm.a.
    
    if (($netpbmlibtype eq "unixshared" or 
         $netpbmlibtype eq "irixshared" or 
         $netpbmlibtype eq "dll") and $netpbmlibsuffix ne "a") {
        print("Do you want to build static libraries too (for linking to \n");
        print("programs not in the Netpbm package?\n");
        print("\n");
        
        my $default = "y";
        
        my $response = prompt("(y)es or (n)o", $default);
        
        if (uc($response) =~ /^(Y|YES)$/)  {
            $staticlib_too = "Y";
        } elsif (uc($response) =~ /^(N|NO)$/)  {
            $staticlib_too = "N";
        } else {
            print("'$response' isn't one of the choices.  \n" .
              "You must choose 'yes' or 'no' (or 'y' or 'n').\n");
            exit 12;
        }
    } else {
        $staticlib_too = "N";
    }
    print("\n");

    $$netpbmlibtypeR   = $netpbmlibtype;
    $$netpbmlibsuffixR = $netpbmlibsuffix;
    $$shlibprefixlistR = $shlibprefixlist;
    $$willBuildSharedR = $willBuildShared;
    $$staticlib_tooR   = $staticlib_too;
}



sub inttypesDefault() {

    my $retval;

    if (defined($testCc)) {

        print("(Doing test compiles to choose a default for you -- " .
              "ignore errors)\n");

        my $cflags = testCflags();

        my $works;

        # We saw a system (Irix 5.3 with native IDO, December 2005) on
        # which sys/types.h defines uint32_t, but not int32_t and other
        # similar types.  We saw a Mac OS X system (January 2006) on which
        # sys/types sometimes defines uint32_t, but sometimes doesn't.
        # So we make those last resorts.

        # int_fastXXX_t are the least likely of all the types to be
        # defined, so we look for that.

        # Solaris 8, for example, has a <sys/inttypes.h> that defines
        # int32_t and uint32_t, but nothing the defines int_fast32_t.

        my @candidateList = ("<inttypes.h>", "<sys/inttypes.h>",
                             "<types.h>", "<sys/types.h>");
        
        for (my $i = 0; $i < @candidateList && !$works; ++$i) {
            my $candidate = $candidateList[$i];
            my @cSourceCode = (
                               "#include $candidate\n",
                               "int_fast32_t testvar;\n"
                               );
            
            testCompile($cflags, \@cSourceCode, \my $success);
            
            if ($success) {
                $works = $candidate;
            }
        }
        if ($works) {
            $retval = $works;
        } else {
            testCompile($cflags, ["int_fast32_t testvar;"], \my $success);
            if ($success) {
                $retval = "NONE";
            } else {
                $retval = '"inttypes_netpbm.h"';
            }
        }
        print("\n");
    } else {
        $retval = '<inttypes.h>';
    }
    return $retval;
}



sub getInttypes($) {
    my ($inttypesHeaderFileR) = @_;

    my $gotit;

    print("What header file defines uint32_t, etc.?\n");
    print("\n");

    my $default = inttypesDefault();
    
    while (!$gotit) {
        my $response = prompt("'#include' argument or NONE", $default);

        if ($response eq "NONE") {
            $$inttypesHeaderFileR = '';
            $gotit = $TRUE;
        } else {
            if ($response !~ m{<.+>} &&
                $response !~ m{".+"}) {
                print("'$response' is not a legal argument of a C #include " .
                      "statement.  It must be something in <> or \"\".\n");
            } else {
                $gotit = $TRUE;
                $$inttypesHeaderFileR = $response;
            }
        }
    }
}



sub getInt64($$) {

    my ($inttypes_h, $haveInt64R) = @_;

    if (defined($testCc)) {

        print("(Doing test compiles to determine if you have int64 type -- " .
              "ignore errors)\n");

        my $cflags = testCflags();

        my $works;

        my @cSourceCode = (
                           "#include $inttypes_h\n",
                           "int64_t testvar;\n"
                           );
            
        testCompile($cflags, \@cSourceCode, \my $success);
            
        if ($success) {
            print("You do.\n");
            $$haveInt64R = 'Y';
        } else {
            print("You do not.  64-bit code won't be built.\n");
            $$haveInt64R = 'N';
        }
        print("\n");
    } else {
        $$haveInt64R = "N";
    }
}



sub determineSseCapability($) {

    my ($haveEmmintrinR) = @_;

    if (defined($testCc)) {

        print("(Doing test compiles to determine if your compiler has SSE " .
              "intrinsics -- ignore errors)\n");

        my $cflags = testCflags();

        my $works;

        my @cSourceCode = (
                           "#include <emmintrin.h>\n",
                           );
            
        testCompile($cflags, \@cSourceCode, \my $success);
            
        if ($success) {
            print("It does.\n");
            $$haveEmmintrinR = $TRUE;
        } else {
            print("It does not.  Programs will not exploit fast SSE " .
                  "instructions.\n");
            $$haveEmmintrinR = $FALSE;
        }
        print("\n");
    } else {
        # We conservatively estimate the facility isn't there
        $$haveEmmintrinR = $FALSE;
    }
}



sub getSse($) {

    my ($wantSseR) = @_;

    determineSseCapability(\my $haveEmmintrin);

    my $gotit;

    print("Use SSE instructions?\n");
    print("\n");

    my $default = $haveEmmintrin ? "y" : "n";

    $$wantSseR = promptYesNo($default);

    # Another complication in the SSE world is that GNU compiler options
    # -msse, -msse2, and -march=xxx affect whether the compiler can or will
    # generate the instructions.  When compiling for older processors, the
    # default for these options is negative ; for newer processors, it is
    # affirmative.  -[no]msse2 determines whether macro __SSE2__ macro is
    # defined.  If it is not, #include <emmintrins.h> fails (<emmintrins.h>
    # checks __SSE2__.

    # The Netpbm build does not mess with these compiler options.  If the
    # user wants something other than the default, he can put it in CFLAGS
    # in config.mk manually or on the make command line on in CFLAGS_PERSONAL.
}


sub getIcon($$) {

    my ($platform, $wantIconR) = @_;

    if ($platform eq 'WINDOWS') {
        print("Include an icon in each executable?\n");
        $$wantIconR = promptYesNo("y");
    } else {
        $$wantIconR = $FALSE;
    }
}



# TODO: These should do test compiles to see if the headers are in the
# default search path, both to create a default to offer and to issue a
# warning after user has chosen.  Also test links to test the link library.

# It looks like these should all be in the default search paths and were there
# just to override defaults in config.mk.in.  Since Configure now
# creates a default of "standard search path," I'm guessing we don't need
# to set these anymore.

sub getTiffLibrary($@) {

    my ($platform, @suggestedHdrDir) = @_;

    my $tifflib;
    {
        my $default = "libtiff" . libSuffix($platform);

        print("What is your TIFF (graphics format) library?\n");
        
        my $response = prompt("library filename or 'none'", $default);
        
        if ($response ne "none") {
            $tifflib = $response;
        }
        if (defined($tifflib) and $tifflib =~ m{/} and !-f($tifflib)) {
            warnUser("No regular file named '$tifflib' exists.");
        }
    }
    my $tiffhdr_dir;
    if (defined($tifflib)) {
        my $default;

        if (-d("/usr/include/tiff")) {
            $default = "/usr/include/tiff";
        } elsif (-d("/usr/include/libtiff")) {
            $default = "/usr/include/libtiff";
        } else {
            $default = "default";
        }
        print("Where are the interface headers for it?\n");
        
        my $response = prompt("TIFF header directory", $default);
        
        if ($response ne "default") {
            $tiffhdr_dir = $response;
        }
        if (defined($tiffhdr_dir) and !-d($tiffhdr_dir)) {
            warnUser("No directory named '$tiffhdr_dir' exists.");
        }
    }
    return($tifflib, $tiffhdr_dir);
}



sub getJpegLibrary($@) {

    my ($platform, @suggestedHdrDir) = @_;

    my $jpeglib;
    {
        my $default = "libjpeg" . libSuffix($platform);

        print("What is your JPEG (graphics format) library?\n");
        
        my $response = prompt("library filename or 'none'", $default);
        
        if ($response ne "none") {
            $jpeglib = $response;
        }
    }
    my $jpeghdr_dir;
    if (defined($jpeglib)) {
        my $default;

        if (-d("/usr/include/jpeg")) {
            $default = "/usr/include/jpeg";
        } else {
            $default = "default";
        }
        print("Where are the interface headers for it?\n");
        
        my $response = prompt("JPEG header directory", $default);
        
        if ($response ne "default") {
            $jpeghdr_dir = $response;
        }
        if (defined($jpeghdr_dir) and !-d($jpeghdr_dir)) {
            warnUser("No directory named '$jpeghdr_dir' exists.");
        }
    }
    return($jpeglib, $jpeghdr_dir);
}



sub getPngLibrary($@) {

    my ($platform, @suggestedHdrDir) = @_;

    my ($pnglib, $pnghdr_dir);

    if (commandExists('libpng-config')) {
        # We don't need to ask where Libpng is; there's a 'libpng-config'
        # That tells exactly how to access it, and the make files will use
        # that.
    } else {
        {
            my $default = "libpng" . libSuffix($platform);

            print("What is your PNG (graphics format) library?\n");
            
            my $response = prompt("library filename or 'none'", $default);
            
            if ($response ne "none") {
                $pnglib = $response;
            }
        }
        if (defined($pnglib)) {
            my $default;

            if (-d("/usr/include/png")) {
                $default = "/usr/include/libpng";
            } else {
                $default = "default";
            }
            
            print("Where are the interface headers for it?\n");
            
            my $response = prompt("PNG header directory", $default);

            if ($response ne "default") {
                $pnghdr_dir = $response;
            }
        }
    }
    return($pnglib, $pnghdr_dir);
}



sub getZLibrary($@) {

    my ($platform, @suggestedHdrDir) = @_;

    my ($zlib, $zhdr_dir);

    {
        my $default = "libz" . libSuffix($platform);

        print("What is your Z (compression) library?\n");
        
        my $response = prompt("library filename or 'none'", $default);
        
        if ($response ne "none") {
            $zlib = $response;
        }
    }
    if (defined($zlib)) {
        my $default;

        if (-d("/usr/include/zlib")) {
            $default = "/usr/include/zlib";
        } else {
            $default = "default";
        }
        
        print("Where are the interface headers for it?\n");
        
        my $response = prompt("Z header directory", $default);
        
        if ($response ne "default") {
            $zhdr_dir = $response;
        }
    }
    return($zlib, $zhdr_dir);
}



sub getX11Library($@) {

    my ($platform, @suggestedHdrDir) = @_;

    my $x11lib;
    my $x11hdr_dir;

    if (system('pkg-config x11 --exists') == 0) {
        # We don't need to ask where X libraries are; pkg-config knows and the
        # make files will use that.
    } else {
        {
            my $default;

            if (-d('/usr/link/X11')) {
                $default = '/usr/link/X11/libX11' . libSuffix($platform);
            } elsif (-d('/usr/X11R6/lib')) {
                $default = '/usr/X11R6/lib/libX11' . libSuffix($platform);
            } else {
                $default = "libX11" . libSuffix($platform);
            }
            print("What is your X11 (X client) library?\n");
            
            my $response = prompt("library filename or 'none'", $default);
            
            if ($response ne "none") {
                $x11lib = $response;
            }
        }
        if (defined($x11lib)) {
            my $default;

            $default = "default";

            print("Where are the interface headers for it?\n");
            
            my $response = prompt("X11 header directory", $default);
            
            if ($response ne "default") {
                $x11hdr_dir = $response;
            }
            if (defined($x11hdr_dir)) {
                if (!-d($x11hdr_dir)) {
                    warnUser("No directory named '$x11hdr_dir' exists.");
                } elsif (!-d("$x11hdr_dir/X11")) {
                    warnUser("Directory '$x11hdr_dir' does not contain " .
                             "the requisite 'X11' subdirectory");
                }
            }
        }
    }
    return($x11lib, $x11hdr_dir);
}



sub getLinuxsvgaLibrary($@) {

    my ($platform, @suggestedHdrDir) = @_;

    my ($svgalib, $svgalibhdr_dir);

    if ($platform eq "GNU") {
        my $default;

        if (-d('/usr/link/svgalib')) {
            $default = '/usr/link/svgalib/libvga.so';
        } elsif (-d('/usr/lib/svgalib')) {
            $default = '/usr/lib/svgalib/libvga.so';
        } elsif (system('ldconfig -p | grep libvga >/dev/null 2>&1') == 0) {
            # &>/dev/null should work above, but on 12.03.26, it caused the
            # return value of system() always to be zero!
            $default = 'libvga.so';
        } elsif (-f('/usr/lib/libvga.a')) {
            $default = '/usr/lib/libvga.a';
        } else {
            $default = 'none';
        }
            
        print("What is your Svgalib library?\n");
        
        my $response = prompt("library filename or 'none'", $default);
            
        if ($response ne 'none') {
            $svgalib = $response;
        }
    }
    if (defined($svgalib) && $svgalib ne 'none') {
        my $default;
        
        if (-d('/usr/include/svgalib')) {
            $default = '/usr/include/svgalib';
        } else {
            $default = "default";
        }
        print("Where are the interface headers for it?\n");
        
        my $response = prompt("Svgalib header directory", $default);
        
        if ($response ne "default") {
            $svgalibhdr_dir = $response;
        }
        if (defined($svgalibhdr_dir)) {
            if (!-d($svgalibhdr_dir)) {
                warnUser("No directory named '$svgalibhdr_dir' exists.");
            }
        }
    }
    return($svgalib, $svgalibhdr_dir);
}



sub symlink_command() {

    my $retval;

    # Some Windows environments don't have symbolic links (or even a
    # simulation via a "ln" command, but have a "cp" command which works
    # in a pinch.  Some Windows environments have "ln", but it won't do
    # symbolic links.
    
    if (commandExists("ln")) {
        # We assume if Perl can do symbolic links, so can Ln, and vice
        # versa.

        my $symlink_exists = eval { symlink("",""); 1 };
        
        if ($symlink_exists) {
            $retval = "ln -s";
        } else {
            $retval = "ln";
        }
    } elsif (commandExists("cp")) {
        $retval = "cp";
    } else {
        # Well, maybe we just made a mistake.
        $retval = "ln -s";
    }
    return $retval;
}



sub help() {

    print("This is the Netpbm custom configuration program.  \n");
    print("It is not GNU Configure.\n");
    print("\n");
    print("There is one optional argument to this program:  The " .
          "name of the file to use as the basis for the config.mk " .
          "file.  Default is 'config.mk.in'\n");
    print("\n");
    print("Otherwise, the program is interactive.\n");
}



sub gnuOptimizeOpt($) {
    my ($gccCommandName) = @_;
#-----------------------------------------------------------------------------
#  Compute the -O compiler flag appropriate for a GNU system.  Ordinarily,
#  this is just -O3.  But many popular GNU systems have a broken compiler
#  that causes -O3 to generate incorrect code (symptom: pnmtojpeg --quality=95
#  generates a syntax error message from shhopt).
#-----------------------------------------------------------------------------
# I don't know what are exactly the cases that Gcc is broken.  I know 
# Red Hat 7.1 and 7.2 and Mandrake 8.2, running gcc 2.96.1, commonly have
# the problem.  But it may be limited to a certain subrelease level or
# CPU type or other environment.  People who have reported the problem have
# reported that Gcc 3.0 doesn't have it.  Gcc 2.95.3 doesn't have it.

# Note that automatic inlining happens at -O3 level, but there are some
# subroutines in Netpbm marked for explicit inlining, and we need to disable
# that inlining too, so we must go all the way down to -O0.

    my @gccVerboseResp = `$gccCommandName --verbose 2>&1`;

    my $brokenCompiler;
    
    if (@gccVerboseResp ==2) {
        if ($gccVerboseResp[1] =~ m{gcc version 2.96}) {
            $brokenCompiler = $TRUE;
        } else {
            $brokenCompiler = $FALSE;
        }
    } else {
        $brokenCompiler = $FALSE;
    }

    my $oOpt;

    if ($brokenCompiler) {
        print("You appear to have a broken compiler which would produce \n");
        print("incorrect code if requested to do inline optimization.\n");
        print("Therefore, I am configuring the build to not do inline \n");
        print("optimization.  This will make some Netpbm programs \n");
        print("noticeably slower.  If I am wrong about your compiler, just\n");
        print("edit config.mk and change -O0 to -O3 near the bottom.\n");
        print("\n");
        print("The problem is known to exist in the GNU Compiler \n");
        print("release 2.96.  If you upgrade, you will not have this \n");
        print("problem.\n");
        print("---------------------------------------------\n");
        print("\n");
        $oOpt = "-O0";
    } else {
        $oOpt = "-O3";
    }
    return $oOpt;
}



sub wnostrictoverflowWorks($) {
    my ($gccCommandName) = @_;

    my ($cFile, $cFileName) = tempFile(".c");

    print $cFile "int x;";
    
    my $compileCommand =
        "$gccCommandName -c -o /dev/null -Wno-strict-overflow $cFileName";
    print ("Doing test compile to see if -Wno-strict-overflow works: "
           . "$compileCommand\n");
    my $rc = system($compileCommand);
    
    unlink($cFileName);
    close($cFile);

    return ($rc == 0);
}



sub gnuCflags($) {
    my ($gccCommandName) = @_;

    my $flags;

    $flags = gnuOptimizeOpt($gccCommandName) . " -ffast-math " .
        " -pedantic -fno-common " . 
        "-Wall -Wno-uninitialized -Wmissing-declarations -Wimplicit " .
        "-Wwrite-strings -Wmissing-prototypes -Wundef " .
        "-Wno-unknown-pragmas ";

    if (wnostrictoverflowWorks($gccCommandName)) {
        # The compiler generates some optimizations based on the assumption
        # that you wouldn't code something that can arithmetically overflow,
        # so adding a positive value to something can only make it bigger.
        # E.g. if (x + y > x), where y is unsigned, is a no-op.  The compiler
        # optionally warns when it makes that assumption.  Sometimes, the
        # compiler is able to do that optimization because of inlining, so the
        # code per se is not ridiculous, it just becomes superfluous in
        # context.  That means you can't code around the warning.  Ergo, we
        # must disable the warning.

        $flags .= '-Wno-strict-overflow';
    }
    return("CFLAGS = $flags\n");
}



sub makeCompilerGcc($) {
    my ($config_mkR) = @_;
    my $compileCommand = 'gcc';
    push(@{$config_mkR}, "CC = $compileCommand\n");
    push(@{$config_mkR}, gnuCflags($compileCommand));
}



sub findProcessManagement($) {
    my ($dontHaveProcessMgmtR) = @_;
#-----------------------------------------------------------------------------
#  Return $TRUE iff the system does not have <sys/wait.h> in its default
#  search path.
#-----------------------------------------------------------------------------
    my $cflags = testCflags();

    my @cSourceCode = (
                       "#include <sys/wait.h>\n",
                       );
    
    testCompile($cflags, \@cSourceCode, \my $success);

    if (!$success) {
        print("Your system does not appear to have <sys/wait.h> in its " .
              "standard compiler include path.  Therefore, we will build " .
              "Netpbm with some function missing (e.g. the pm_system() " .
              "function in libnetpbm and most of 'pamlookup'\n");
        $$dontHaveProcessMgmtR = $TRUE;
    } else {
        $$dontHaveProcessMgmtR = $FALSE;
    }
}



sub validateLibraries(@) {
    my @libList = @_;
#-----------------------------------------------------------------------------
#  Check each library name in the list @libList for viability.
#-----------------------------------------------------------------------------
    foreach my $libname (@libList) {
        if (defined($libname)) {
            if ($libname =~ m{/} and !-f($libname)) {
                warnUser("No regular file named '$libname' exists.");
            } elsif (!($libname =~ m{ .* \. (so|a|sa|sl|dll|dylib) $ }x)) {
                warnUser("The library name '$libname' does not have " .
                         "a conventional suffix (.so, .a, .dll, etc.)");
            }
        }
    }
}



sub warnJpegTiffDependency($$) {
    my ($jpeglib, $tifflib) = @_;

    if (defined($tifflib) && !defined($jpeglib)) {
        warnUser("You say you have a Tiff library, but no Jpeg library.  " .
                 "Sometimes the Tiff library prerequires the " .
                 "Jpeg library.  If that is the case on your system, " .
                 "you will have some links fail with " .
                 "missing 'jpeg...' symbols.  If so, rerun " .
                 "Configure and say you have no Tiff library either.");
    }
}



sub testCompileJpeglibH($$) {
    my ($cflags, $successR) = @_;
#-----------------------------------------------------------------------------
#  Do a test compile to see if we can see jpeglib.h.
#-----------------------------------------------------------------------------
    my @cSourceCode = (
                       "#include <ctype.h>\n",
                       "#include <stdio.h>\n",
                       "#include <jpeglib.h>\n",
                       );
    
    testCompile($cflags, \@cSourceCode, $successR);
}



sub testCompileJpegMarkerStruct($$) {
    my ($cflags, $successR) = @_;
#-----------------------------------------------------------------------------
#  Do a test compile to see if struct jpeg_marker_struct is defined in 
#  jpeglib.h.  Assume it is already established that the compiler works
#  and can find jpeglib.h.
#-----------------------------------------------------------------------------
    my @cSourceCode = (
                       "#include <ctype.h>\n",
                       "#include <stdio.h>\n",
                       "#include <jpeglib.h>\n",
                       "struct jpeg_marker_struct test;\n",
                       );

    testCompile($cflags, \@cSourceCode, $successR);
}



sub printMissingHdrWarning($$) {

    my ($name, $hdr_dir) = @_;

    warnUser("You said the compile-time part of the $name library " .
             "(the header files) is in " .
             
             (defined($hdr_dir) ?
              "directory '$hdr_dir', " :
              "the compiler's default search path, ") .

             "but a test compile failed to confirm that.  " .
             "If your configuration is exotic, the test compile might" .
             "just be wrong, but otherwise the Netpbm build will fail.  " .
             "To fix this, either install the $name library there " .
             "or re-run Configure and answer the question about the $name " .
             "library differently."
        );
}



sub printOldJpegWarning() {
    warnUser("Your JPEG library appears to be too old for Netpbm.  " .
             "We base this conclusion on the fact that jpeglib.h apparently" .
             "does not define struct jpeg_marker_struct.  " .
             "If the JPEG library is not " .
             "Independent Jpeg Group's Version 6b" .
             "or better, the Netpbm build will fail when it attempts " .
             "to build the parts that use the JPEG library.  " .
             "If your configuration is exotic, " .
             "this test may just be wrong.  " .
             "Otherwise, either upgrade your JPEG library " .
             "or re-run Configure and say you don't have a JPEG library."
        );
}



sub testJpegHdr($) {

    my ($jpeghdr_dir) = @_;

    if (defined($testCc)) {

        my $generalCflags = testCflags();

        my $jpegIOpt = $jpeghdr_dir ? "-I$jpeghdr_dir" : "";

        testCompileJpeglibH("$generalCflags $jpegIOpt", \my $success);

        if (!$success) {
            print("\n");
            printMissingHdrWarning("JPEG", $jpeghdr_dir);
        } else {
            # We can get to something named jpeglib.h, but maybe it's an old
            # version we can't use.  Check it out.
            testCompileJpegMarkerStruct("$generalCflags $jpegIOpt", 
                                        \my $success);
            if (!$success) {
                print("\n");
                printOldJpegWarning();
            }
        }
    }
}



sub warnJpegNotInDefaultPath($) {
    my ($jpegLib) = @_;

    print("You said your JPEG library is '$jpegLib', which says it is "
          . "in the linker's default search path, but a test link we did "
          . "failed as if it is not.  If it isn't, the build will fail\n");
}



sub testJpegLink($) {
    my ($jpegLib) = @_;
#-----------------------------------------------------------------------------
#  See if we can link the JPEG library with the information user gave us.
#  $jpegLib is the answer to the "what is your JPEG library" prompt, so
#  it is either a library file name such as "libjpeg.so", in the linker's
#  default search path, or it is an absolute path name such as
#  "/usr/jpeg/lib/libjpeg.so".
#
#  We actually test only the default search path case, since users often
#  incorrectly specify that by taking the default.
#-----------------------------------------------------------------------------
    if ($jpegLib =~ m{( lib | cyg ) (.+) \. ( so | a )$}x) {
        my $libName = $2;

        # It's like "libjpeg.so", so is a library in the default search path.
        # $libName is "jpeg" in this example.

        # First we test our test tool.  We can do this only with GCC.

        my @emptySource;

        testCompileLink('-nostartfiles', \@emptySource, \my $controlWorked);

        if ($controlWorked) {
            # The "control" case worked.  Now see if it still works when we add
            # the JPEG library.

            testCompileLink("-nostartfiles -l$libName", \@emptySource,
                            \my $workedWithJpeg);

            if (!$workedWithJpeg) {
                warnJpegNotInDefaultPath($jpegLib);
            }
        }
    }
}



sub testCompileZlibH($$) {
    my ($cflags, $successR) = @_;
#-----------------------------------------------------------------------------
#  Do a test compile to see if we can see zlib.h.
#-----------------------------------------------------------------------------
    my @cSourceCode = (
                       "#include <zlib.h>\n",
                       );
    
    testCompile($cflags, \@cSourceCode, $successR);
}



sub testCompilePngH($$) {
    my ($cflags, $successR) = @_;
#-----------------------------------------------------------------------------
#  Do a test compile to see if we can see png.h, assuming we can see
#  zlib.h, which png.h #includes.
#-----------------------------------------------------------------------------
    my @cSourceCode = (
                       "#include <png.h>\n",
                       );
    
    testCompile($cflags, \@cSourceCode, $successR);
}



sub testPngHdr($$) {
#-----------------------------------------------------------------------------
#  Issue a warning if the compiler can't find png.h.
#-----------------------------------------------------------------------------
    my ($pnghdr_dir, $zhdr_dir) = @_;

    if (defined($testCc)) {

        my $generalCflags = testCflags();

        my $zlibIOpt = $zhdr_dir ? "-I$zhdr_dir" : "";

        testCompileZlibH("$generalCflags $zlibIOpt", \my $success);
        if (!$success) {
            print("\n");
            printMissingHdrWarning("Zlib", $zhdr_dir);
        } else {
            my $pngIOpt = $pnghdr_dir ? "-I$pnghdr_dir" : "";

            testCompilePngH("$generalCflags $zlibIOpt $pngIOpt", 
                            \my $success);

            if (!$success) {
                print("\n");
                printMissingHdrWarning("PNG", $pnghdr_dir);
            }
        }
    }
}



sub printBadPngConfigCflagsWarning($) {
    my ($pngCFlags) = @_;

    warnUser("'libpng-config' in this environment (a program in your PATH) " .
             "gives instructions that don't work for compiling for " .
             "(not linking with) the PNG library.  " .
             "Our test compile failed.  " .
             "This indicates Libpng is installed incorrectly " .
             "on this system.  If so, your Netpbm build, " .
             "which uses 'libpng-config', will fail.  " .
             "But it might also just be our test that is broken.");
}



sub pngLinkWorksWithLzLmMsg() {

    return
        "When we added \"-lz -lm\" to the linker flags, the link worked.  " .
        "That means the fix for this may be to modify 'libpng-config' " .
        "so that 'libpng-config --ldflags' includes \"-lz -lm\" " .
        "in its output.  But the right fix may actually " .
        "be to build libpng differently so that it specifies its dependency " .
        "on those libraries, or to put those libraries in a different " .
        "place, or to create missing symbolic links.";
}



sub printBadPngConfigLdflagsWarning($$) {
    my ($pngLdFlags, $lzLmSuccess) = @_;

    warnUser(
        "'libpng-config' in this environment (a program in your PATH) " .
        "gives instructions that don't work for linking " .
        "with the PNG library.  Our test link failed.  " .
        "This indicates Libpng is installed incorrectly on this system.  " .
        "If so, your Netpbm build, which uses 'libpng-config', will fail.  " .
        "But it might also just be our test that is broken.  " .
        ($lzLmSuccess ? pngLinkWorksWithLzLmMsg : "")
        );
}



sub testLinkPnglib($$) {
    my ($generalCflags, $pngCflags) = @_;

    my @cSourceCode = (
                       "#include <png.h>\n",
                       "int main() {\n",
                       "png_create_write_struct(0, NULL, NULL, NULL);\n",
                       "}\n",
                       );

    testCompile("$generalCflags $pngCflags", \@cSourceCode, \my $success);
    if (!$success) {
        # Since it won't compile, we can't test the link
    } else {
        my $pngLdflags = qx{libpng-config --ldflags};
        chomp($pngLdflags);
        
        testCompileLink("$generalCflags $pngCflags $pngLdflags",
                        \@cSourceCode, \my $success);
        
        if (!$success) {
            testCompileLink("$generalCflags $pngCflags $pngLdflags -lz -lm",
                        \@cSourceCode, \my $lzLmSuccess);

            printBadPngConfigLdflagsWarning($pngLdflags, $lzLmSuccess);
        }
    }
}



sub testLibpngConfig() {
#-----------------------------------------------------------------------------
#  Issue a warning if the instructions 'libpng-config' give for compiling
#  with Libpng don't work.
#-----------------------------------------------------------------------------
    my $generalCflags = testCflags();

    my $pngCflags = qx{libpng-config --cflags};
    chomp($pngCflags);

    testCompilePngH("$generalCflags $pngCflags", \my $success);

    if (!$success) {
        printBadPngConfigCflagsWarning($pngCflags);
    } else {
        testLinkPnglib($generalCflags, $pngCflags);
    }
}



sub testCompileXmlreaderH($$) {
    my ($cflags, $successR) = @_;
#-----------------------------------------------------------------------------
#  Do a test compile to see if we can see xmlreader.h.
#-----------------------------------------------------------------------------
    my @cSourceCode = (
                       "#include <libxml/xmlreader.h>\n",
                       );
    
    testCompile($cflags, \@cSourceCode, $successR);
}



sub printBadXml2CFlagsWarning($) {
    my ($xml2CFlags) = @_;

    warnUser(
      "'xml2-config' in this environment (a program in your PATH) " .
        "gives instructions that don't work for compiling for the " .
        "Libxml2 library.  Our test compile failed.  " .
        "This indicates Libxml2 is installed incorrectly on this system.  " .
        "If so, your Netpbm build, which uses 'xml2-config', will fail.  " .
        "But it might also just be our test that is broken.  ".
        "'xml2-config' says to use compiler options '$xml2CFlags'.  "
        );
}



sub testCompileXmlReaderTypes($$) {
    my ($cflags, $successR) = @_;
#-----------------------------------------------------------------------------
#  Do a test compile to see if xmlreader.h defines xmlReaderTypes,
#  assuming we can compile with xmlreader.h in general.
#-----------------------------------------------------------------------------
    my @cSourceCode = (
                       "#include <libxml/xmlreader.h>\n",
                       "xmlReaderTypes dummy;\n",
                       );
    
    testCompile($cflags, \@cSourceCode, $successR);
}



sub printMissingXmlReaderTypesWarning() {

    warnUser(
        "Your libxml2 interface header file does not define the type " .
        "'xmlReaderTypes', which Netpbm needs.  " .
        "In order to build Netpbm successfully, " .
        "you must install a more recent version of Libxml2.  "
        );
}



sub printNoLibxml2Warning() {

    warnUser(
        "You appear not to have Libxml2 installed ('xml2-config' does not " .
        "exist in your program search PATH).  " .
        "If this is the case at build time, " .
        "the build will skip building 'svgtopam'."
        );
}



sub testLibxml2Hdr() {
#-----------------------------------------------------------------------------
#  Issue a warning if the instructions 'xml2-config' give for compiling
#  with Libxml2 don't work.  In particular, note whether they get us a
#  modern enough Libxml2 to have the 'xmlReaderTypes' type.
#-----------------------------------------------------------------------------
    if (commandExists('xml2-config')) {
        my $generalCflags = testCflags();

        my $xml2Cflags = qx{xml2-config --cflags};
        chomp($xml2Cflags);

        testCompileXmlreaderH("$generalCflags $xml2Cflags", \my $success);

        if (!$success) {
            printBadXml2CflagsWarning($xml2Cflags);
        } else {
            testCompileXmlReaderTypes("$generalCflags $xml2Cflags",
                                      \my $success);

            if (!$success) {
                printMissingXmlReaderTypesWarning();
            }
        }
    } else {
        printNoLibxml2Warning();
    }
}



sub testConfiguration($$$$$$) {

    my ($jpeglib, $jpeghdr_dir,
        $pnglib, $pnghdr_dir, $zlib, $zhdr_dir) = @_;

    if (defined($jpeglib)) {
        testJpegHdr($jpeghdr_dir);

        testJpegLink($jpeglib);
    }
    if (defined($pnglib) && defined($zlib)) {
        testPngHdr($pnghdr_dir, $zhdr_dir);
    } elsif (commandExists('libpng-config')) {
        testLibpngConfig();
    }
    testLibxml2Hdr();

    # TODO: We ought to validate other libraries too.  But it's not
    # that important, because in the vast majority of cases where the
    # user incorrectly identifies any library, it affects all the
    # libraries and if the user can get a handle on the JPEG library
    # problem, he will also solve problems with any other library.
}



#******************************************************************************
#
#  MAINLINE
#
#*****************************************************************************

autoFlushStdout();

my $configInPathArg;
if (@ARGV > 0) {
    if ($ARGV[0] =~ "^-") {
        if ($ARGV[0] eq "--help") {
            help();
            exit(0);
        } else {
            die("Unrecognized option: $ARGV[0]");
        }
    } 
    $configInPathArg = $ARGV[0];
}

if (stat("config.mk")) {
    print("Discard existing config.mk?\n");
    print("Y or N (N) ==> ");

    my $answer = <STDIN>;
    if (!defined($answer)) {
        die("\nEnd of file on Standard Input");
    }
    chomp($answer);
    if (uc($answer) ne "Y") {
        print("Aborting at user request.\n");
        exit(1);
    }
}

print("\n");

displayIntroduction();

my ($platform, $subplatform) = getPlatform();

print("\n");

if ($platform eq "NONE") {
    print("You will have to construct config.mk manually.  To do \n");
    print("this, copy config.mk.in as config.mk, and then \n");
    print("edit it.  Follow the instructions and examples in the file. \n");
    print("Please report your results to the Netpbm maintainer so he \n");
    print("can improve the configure program. \n");
    exit;
}

getCompiler($platform, $subplatform, \my $compiler);

getLinker($platform, $compiler, \my $baseLinker, \my $linkViaCompiler);

chooseTestCompiler($compiler, \$testCc);

my $netpbmlib_runtime_path;
    # Undefined if the default from config.mk.in is acceptable.

if ($platform eq "SOLARIS" or $platform eq "IRIX" or
    $platform eq "DARWIN" or $platform eq "NETBSD" or
    $platform eq "AMIGA") {
    print("Where will the Netpbm shared library reside once installed?\n");
    print("Enter 'default' if it will reside somewhere that the shared\n");
    print("library loader will find it automatically.  Otherwise, \n");
    print("this directory will get built into the Netpbm programs.\n");
    print("\n");

    my $default = "default";
    my $response = prompt("Netpbm shared library directory", $default);

    if ($response eq "default") {
        $netpbmlib_runtime_path = "";
    } else {
        $netpbmlib_runtime_path = $response;
    }
}

my $default_target;

print("Do you want a regular build or a merge build?\n");
print("If you don't know what this means, " .
      "take the default or see doc/INSTALL\n");
print("\n");

{
    my $default = "regular";
    my $response = prompt("regular or merge", $default);
    
    if ($response eq "regular") {
        $default_target = "nonmerge";
    } elsif ($response eq "merge") {
        $default_target = "merge";
    } else {
        print("'$response' isn't one of the choices.  \n" .
              "You must choose 'regular' or 'merge'.\n");
        exit 12;
    }
}

print("\n");

getLibTypes($platform, $subplatform, $default_target,
            \my $netpbmlibtype, \my $netpbmlibsuffix, \my $shlibprefixlist,
            \my $willBuildShared, \my $staticlib_too);


getInttypes(\my $inttypesHeaderFile);

getInt64($inttypesHeaderFile, \my $haveInt64);

getSse(\my $wantSse);

findProcessManagement(\my $dontHaveProcessMgmt);

getIcon($platform, \my $wantIcon);

#******************************************************************************
#
#  FIND THE PREREQUISITE LIBRARIES
#
#*****************************************************************************

print << 'EOF';


The following questions concern the subroutine libraries that are Netpbm
prerequisites.  Every library has a compile-time part (header files)
and a link-time part.  In the case of a shared library, these are both
part of the "development" component of the library, which may be separately
installable from the runtime shared library.  For each library, you must
give the filename of the link library.  If it is not in your linker's
default search path, give the absolute pathname of the file.  In addition,
you will be asked for the directory in which the library's interface headers
reside, and you can respond 'default' if they are in your compiler's default
search path.

If you don't have the library on your system, you can enter 'none' as the
library filename and the builder will skip any part that requires that
library.

EOF

my ($jpeglib, $jpeghdr_dir) = getJpegLibrary($platform);
print("\n");
my ($tifflib, $tiffhdr_dir) = getTiffLibrary($platform, $jpeghdr_dir);
print("\n");
my ($pnglib, $pnghdr_dir)   = getPngLibrary($platform, 
                                            $tiffhdr_dir, $jpeghdr_dir);
print("\n");
my ($zlib, $zhdr_dir)       = getZLibrary($platform, 
                                          $pnghdr_dir,
                                          $tiffhdr_dir,
                                          $jpeghdr_dir);
print("\n");
my ($x11lib, $x11hdr_dir) = getX11Library($platform); 

print("\n");
my ($linuxsvgalib, $linuxsvgahdr_dir) = getLinuxsvgaLibrary($platform); 

print("\n");

# We should add the JBIG and URT libraries here too.  They're a little
# more complicated because there are versions shipped with Netpbm.


#******************************************************************************
#
#  CONFIGURE DOCUMENTATION
#
#*****************************************************************************

print("What URL will you use for the main Netpbm documentation page?\n");
print("This information does not get built into any programs or libraries.\n");
print("It does not make anything actually install that web page.\n");
print("It is just for including in legacy man pages.\n");
print("\n");

my $default = "http://netpbm.sourceforge.net/doc/";

my $netpbm_docurl = prompt("Documentation URL", $default);

print("\n");




#******************************************************************************
#
#  VALIDATE THE CONFIGURATION USER HAS SELECTED
#
#*****************************************************************************

validateLibraries($jpeglib, $tifflib, $pnglib, $zlib);

warnJpegTiffDependency($jpeglib, $tifflib);

testConfiguration($jpeglib, $jpeghdr_dir,
                  $pnglib, $pnghdr_dir,
                  $zlib, $zhdr_dir,
                  );

#******************************************************************************
#
#  FIND THE NETPBM SOURCE TREE AND INITIALIZE BUILD TREE
#
#*****************************************************************************

my $defaultConfigInPath;

if (-f("GNUmakefile")) {
    # He's apparently running us in the source tree or an already set up
    # build directory.
    $defaultConfigInPath = "config.mk.in";
} else {
    my $srcdir;
    my $done;

    $done = $FALSE;
    while (!$done) {
        print("Where is the Netpbm source code?\n");

        $srcdir = prompt("Netpbm source directory", 
                         abs_path(dirname($0) . "/.."));

        if (-f("$srcdir/GNUmakefile")) {
            $done = $TRUE;
        } else {
            print("That doesn't appear to contain Netpbm source code.\n");
            print("There is no file named 'GNUmakefile' in it.\n");
            print("\n");
        }    
    }
    unlink("GNUmakefile");
    symlink("$srcdir/GNUmakefile", "GNUmakefile");
    unlink("Makefile");
    symlink("$srcdir/Makefile", "Makefile");

    open(SRCDIR, ">srcdir.mk");
    print(SRCDIR "SRCDIR = $srcdir\n");
    close(SRCDIR);
    
    $defaultConfigInPath = "$srcdir/config.mk.in";
}

#******************************************************************************
#
#  BUILD config.mk
#
#*****************************************************************************

my @config_mk;
    # This is the complete config.mk contents.  We construct it here
    # and ultimately write the whole thing out as config.mk.

# First, we just read the 'config.mk.in' in

my $configInPath;
if (defined($configInPathArg)) {
    $configInPath = $configInPathArg;
} else {
    $configInPath = $defaultConfigInPath;
}
open (CONFIG_IN,"<$configInPath") or
    die("Unable to open file '$configInPath' for input.");

@config_mk = <CONFIG_IN>;

unshift(@config_mk, 
        "####This file was automatically created by 'configure.'\n",
        "####Many variables are set twice -- a generic setting, then \n",
        "####a system-specific override at the bottom of the file.\n",
        "####\n");

close(CONFIG_IN);

# Now, add the variable settings that override the default settings that are
# done by the config.mk.in contents.

push(@config_mk, "\n\n\n\n");
push(@config_mk, "####Lines above were copied from config.mk.in " .
     "by 'configure'.\n");
push(@config_mk, "####Lines below were added by 'configure' based on " .
     "the $platform platform.\n");
if (defined($subplatform)) {
    push(@config_mk, "####subplatform '$subplatform'\n");
}

push(@config_mk, "DEFAULT_TARGET = $default_target\n");

push(@config_mk, "NETPBMLIBTYPE=$netpbmlibtype\n");
push(@config_mk, "NETPBMLIBSUFFIX=$netpbmlibsuffix\n");
if (defined($shlibprefixlist)) {
    push(@config_mk, "SHLIBPREFIXLIST=$shlibprefixlist\n");
}
push(@config_mk, "STATICLIB_TOO=$staticlib_too\n");

if (defined($netpbmlib_runtime_path)) {
    push(@config_mk, "NETPBMLIB_RUNTIME_PATH=$netpbmlib_runtime_path\n");
}

if ($platform eq "GNU") {
    if (!commandExists("cc") && commandExists("gcc")) {
        makeCompilerGcc(\@config_mk);
    } else {
        push(@config_mk, gnuCflags('cc'));
    }
# The merged programs have a main_XXX subroutine instead of main(),
# which would cause a warning with -Wmissing-declarations or 
# -Wmissing-prototypes.
    push(@config_mk, "CFLAGS_MERGE = " .
         "-Wno-missing-declarations -Wno-missing-prototypes\n");
    push(@config_mk, "LDRELOC = ld --reloc\n");
    push(@config_mk, "LINKER_CAN_DO_EXPLICIT_LIBRARY=Y\n");
} elsif ($platform eq "SOLARIS") {
    push(@config_mk, 'LDSHLIB = -Wl,-Bdynamic,-G,-h,$(SONAME)', "\n");

    push(@config_mk, 'NEED_RUNTIME_PATH = Y', "\n");
    if ($compiler eq "cc") {
        push(@config_mk, "CFLAGS = -O\n");
        push(@config_mk, "CFLAGS_SHLIB = -Kpic\n");
    } else {
        makeCompilerGcc(\@config_mk);
    }
    # Before Netpbm 10.20 (January 2004), we set this to -R for 
    # $compiler == cc and -rpath otherwise.  But now we know that the GNU
    # compiler can also invoke a linker that needs -R, so we're more flexible.
    if ($baseLinker eq "GNU") {
        push(@config_mk, "RPATHOPTNAME = -rpath\n");
    } else {
        push(@config_mk, "RPATHOPTNAME = -R\n");
    }
    push(@config_mk, "NETWORKLD = -lsocket -lnsl\n");
} elsif ($platform eq "HP-UX") {
    if ($compiler eq "gcc") {
        makeCompilerGcc(\@config_mk);
        push(@config_mk, "CFLAGS += -fPIC\n");
        push(@config_mk, "LDSHLIB = -shared -fPIC\n");
        push(@config_mk, 'LDFLAGS += -Wl,+b,/usr/pubsw/lib', "\n");
    } else {
        # We don't know what to do here.  We used to (before 10.20) just
        # just assume the compiler was gcc.  We know that the gcc stuff
        # above does NOT work for HP native compiler.
        push(@config_mk, "LDSHLIB =\n");
    }
} elsif ($platform eq "AIX") {
    push(@config_mk, 'LDFLAGS += -L /usr/pubsw/lib', "\n");
    if ($compiler eq "cc") {
        # Yes, the -L option implies the runtime as well as linktime library
        # search path.  There's no way to specify runtime path independently.
        push(@config_mk, "RPATHOPTNAME = -L\n");
        push(@config_mk, "LDSHLIB = -qmkshrobj\n");
    } else {
        makeCompilerGcc(\@config_mk);
        push(@config_mk, "LDSHLIB = -shared\n");
    }
} elsif ($platform eq "TRU64") {
#    push(@config_mk, "INSTALL = installbsd\n");
    if ($compiler eq "cc") {
        push(@config_mk, 'CFLAGS = -O2 -std1', "\n");
        push(@config_mk, "LDFLAGS = -call_shared -oldstyle_liblookup " .
             "-L/usr/local/lib\n");
        push(@config_mk, "LDSHLIB = -shared -expect_unresolved \"*\"\n");
    } else {
        # We've never tested this.  This is just here to give a user a 
        # headstart on submitting to us the necessary information.  2002.07.04.
        push(@config_mk, "CC = gcc\n");
        push(@config_mk, 'CFLAGS = -O3', "\n");
        push(@config_mk, "LDSHLIB = -shared\n");
    }
    # Between May 2000 and July 2003, we had -DLONG_32 in these options.
    # We took it out because it generated bad code for a TRU64 user in
    # July 2003 whose system has 64 bit long and 32 bit int.  It affects
    # only Ppmtompeg and it isn't clear that using long instead of int is
    # ever right anyway.

    push(@config_mk, "OMIT_NETWORK = Y\n");
    push(@config_mk, "LINKER_CAN_DO_EXPLICIT_LIBRARY=Y\n");
} elsif ($platform eq "IRIX") {
#    push(@config_mk, "INSTALL = install\n");
    push(@config_mk, "MANPAGE_FORMAT = cat\n");
    push(@config_mk, "RANLIB = true\n");
    push(@config_mk, "CFLAGS = -n32 -O3 -fullwarn\n");
    push(@config_mk, "LDFLAGS = -n32\n");
    push(@config_mk, "LDSHLIB = -shared -n32\n");
} elsif ($platform eq "WINDOWS") {
    if ($subplatform eq "cygwin") {
        makeCompilerGcc(\@config_mk);
    }
    push(@config_mk, "EXE = .exe\n");
    push(@config_mk, "OMIT_NETWORK = Y\n");
#    # Though it may not have the link as "ginstall", "install" in a Windows
#    # Unix environment is usually GNU install.
#    my $ginstall_result = `ginstall --version 2>/dev/null`;
#    if (!$ginstall_result) {
#        # System doesn't have 'ginstall', so use 'install' instead.
#        push(@config_mk, "INSTALL = install\n");
#    }
    push(@config_mk, 'SYMLINK = ', symlink_command(), "\n");
    push(@config_mk, 'DLLVER=$(NETPBM_MAJOR_RELEASE)', "\n");
    push(@config_mk, "LDSHLIB = " . 
         '-shared -Wl,--image-base=0x10000000 -Wl,--enable-auto-import', "\n");
    if ($subplatform ne "cygwin") {
        push(@config_mk, "MSVCRT = Y\n");
    }
    if ($wantIcon) {
        push(@config_mk, 'WINICON_OBJECT = $(BUILDDIR)/icon/netpbm.o', "\n");
    }
} elsif ($platform eq "BEOS") {
    push(@config_mk, "LDSHLIB = -nostart\n");
} elsif ($platform eq "OPENBSD") {
    # vedge@vedge.com.ar says on 2001.04.29 that there are a ton of 
    # undefined symbols in the Fiasco stuff on OpenBSD.  So we'll just
    # cut it out of the build until someone feels like fixing it.
    push(@config_mk, "BUILD_FIASCO = N\n");
} elsif ($platform eq "FREEBSD") {
} elsif ($platform eq "AMIGA") {
    push(@config_mk, "CFLAGS = -m68020-60 -ffast-math -mstackextend\n");
} elsif ($platform eq "UNIXWARE") {
    # Nothing to do.
} elsif ($platform eq "SCO") {
    # Got this from "John H. DuBois III" <spcecdt@armory.com> 2002.09.27:
    push(@config_mk, "RANLIB = true\n");
    if ($compiler eq "cc") {
        push(@config_mk, "CFLAGS = -O\n");
        push(@config_mk, "CFLAGS_SHLIB = -O -K pic\n");
        push(@config_mk, "LDSHLIB = -G\n");
        push(@config_mk, "SHLIB_CLIB =\n");
    } else {
        makeCompilerGcc(\@config_mk);
        push(@config_mk, "LDSHLIB = -shared\n"); 
    }
    push(@config_mk, "NETWORKLD = -lsocket -lresolve\n");
} elsif ($platform eq "DARWIN") {
    push(@config_mk, "CC = cc -no-cpp-precomp\n");
    push(@config_mk, gnuCflags('cc'));
    push(@config_mk, 'CFLAGS_SHLIB = -fno-common', "\n");

    my $installNameOpt;
    if ($netpbmlib_runtime_path eq '') {
        $installNameOpt = '';
    } else {
        $installNameOpt  =
            '-install_name $(NETPBMLIB_RUNTIME_PATH)/libnetpbm.$(MAJ).dylib';
    }
    push(@config_mk, "LDSHLIB = -dynamiclib $installNameOpt\n");
#    push(@config_mk, "INSTALL = install\n");
} else {
    die ("Internal error: invalid value for \$platform: '$platform'\n");
}

if ($linkViaCompiler) {
    push(@config_mk, "LINKERISCOMPILER = Y\n");
}

my $flex_result = `flex --version`;
if (!$flex_result) {
    # System doesn't have 'flex'.  Maybe 'lex' will work.  See the
    # make rules for Thinkjettopbm for information on our experiences
    # with Lexes besides Flex.

    my $systemRc = system('lex </dev/null >/dev/null 2>&1');

    if ($systemRc >> 8 == 127) {
        print("\n");
        print("You do not appear to have the 'flex' or 'lex' pattern \n");
        print("matcher generator on your system, so we will not build \n");
        print("programs that need it (Thinkjettopbm)\n");
        
        print("\n");
        print("Press ENTER to continue.\n");
        my $key = <STDIN>;
        print("\n");

        push(@config_mk, "LEX=\n");
    } else {
        print("\n");
        print("Using 'lex' as the pattern matcher generator, " .
              "since we cannot\n");
        print("find 'flex' on your system.\n");
        print("\n");

        push(@config_mk, "LEX = lex\n"); 
    }
}

if ($compiler eq 'gcc') {
    push(@config_mk, "CFLAGS_SHLIB += -fPIC\n");
}

if (defined($tiffhdr_dir)) {
    push(@config_mk, "TIFFHDR_DIR = $tiffhdr_dir\n");
}
if (defined($tifflib)) {
    push(@config_mk, "TIFFLIB = $tifflib\n");
}

if (defined($jpeghdr_dir)) {
    push(@config_mk, "JPEGHDR_DIR = $jpeghdr_dir\n");
}
if (defined($jpeglib)) {
    push(@config_mk, "JPEGLIB = $jpeglib\n");
}

if (defined($pnghdr_dir)) {
    push(@config_mk, "PNGHDR_DIR = $pnghdr_dir\n");
}
if (defined($pnglib)) {
    push(@config_mk, "PNGLIB = $pnglib\n");
}

if (defined($zhdr_dir)) {
    push(@config_mk, "ZHDR_DIR = $zhdr_dir\n");
}
if (defined($zlib)) {
    push(@config_mk, "ZLIB = $zlib\n");
}

if (defined($x11hdr_dir)) {
    push(@config_mk, "X11HDR_DIR = $x11hdr_dir\n");
}
if (defined($x11lib)) {
    push(@config_mk, "X11LIB = $x11lib\n");
}

if (defined($linuxsvgahdr_dir)) {
    push(@config_mk, "LINUXSVGAHDR_DIR = $linuxsvgahdr_dir\n");
}
if (defined($linuxsvgalib)) {
    push(@config_mk, "LINUXSVGALIB = $linuxsvgalib\n");
}

if (defined($netpbm_docurl)) {
    push(@config_mk, "NETPBM_DOCURL = $netpbm_docurl\n");
}

if ($inttypesHeaderFile ne '<inttypes.h>') {
    push(@config_mk, "INTTYPES_H = $inttypesHeaderFile\n");
}

if ($haveInt64 ne 'Y') {
    push(@config_mk, "HAVE_INT64 = $haveInt64\n");
}

if ($wantSse) {
    push(@config_mk, "WANT_SSE = Y\n");
}

if ($dontHaveProcessMgmt) {
    push(@config_mk, "DONT_HAVE_PROCESS_MGMT = Y\n");
}

#******************************************************************************
#
#  WRITE OUT THE FILE
#
#*****************************************************************************

open(config_mk, ">config.mk") or
    die("Unable to open config.mk for writing in the current " .
        "directory.");

print config_mk @config_mk;

close(config_mk) or
    die("Error:  Close of config.mk failed.\n");

print("\n");
print("We have created the file 'config.mk'.  You may want to look \n");
print("at it and edit it to your requirements and taste before doing the \n");
print("make.\n");
print("\n");

print("Now you may proceed with 'make'\n");
if ($warned) {
    print("BUT: per the previous WARNINGs, don't be surprised if it fails\n");
}
print("\n");


exit 0;          

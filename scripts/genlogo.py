# Generates src/logo_data.inc from hand-drawn, original sysglance ASCII art.
# Markers: $1 = primary color, $2 = secondary color.
logos = [
 ("sysglance", "", 36, 34, r"""
$1        .---------.
$1     .-'  $2.-----.$1  '-.
$1   .'   $2.'  ___  '.$1   '.
$1  (    $2|   (@@@)   |$1    )
$1   '.   $2'.  ---  .'$1   .'
$1     '-.  $2'-----'$1  .-'
$1        '---------'
$2         sysglance
"""),
 ("ubuntu", "ubuntu kubuntu xubuntu lubuntu pop linuxmint elementary zorin neon", 33, 31, r"""
$2           ___
$1      .--$2(   )$1--.
$1    .'    $2'-'$1    '.
$1   /    .-----.    \
$2 ($1   |  /       \  |
$2(   )$1  |       |   |
$2 '-'$1 \  \       /  /
$1     '. '-----' .$2___
$1       '-.....-$2(   )
$2                '-'
"""),
 ("arch", "arch archarm manjaro endeavouros garuda artix", 36, 34, r"""
$1            .
$1           / \
$1          /   \
$1         / /\  \
$1        / /  \  \
$1       / /    \  \
$2      / /  ..  \  \
$2     / /  /  \  \  \
$2    /_/  /    \  \__\
$2   '-'  '      '  '-'
"""),
 ("fedora", "fedora rhel centos rocky almalinux nobara", 34, 37, r"""
$1        .-------.
$1      .'         '.
$1     /     $2.---.$1   \
$1    |      $2|$1        |
$1    |   $2---+---$1     |
$1    |      $2|$1        |
$1    |  $2.___/$1       /
$1     \            .'
$1      '----------'
"""),
 ("alpine", "alpine postmarketos", 34, 37, r"""
$2              .
$2             / \        .
$1            /   \      / \
$1           /  $2/\$1 \    /   \
$1          /  $2/  \$1 \  /     \
$1         /  /    \ \/  /\   \
$1        /  /      \   /  \   \
$1       /__/________\_/____\___\
"""),
 ("debian", "debian raspbian devuan kali", 31, 37, r"""
$1        _.-----._
$1      .'  _.--._ '.
$1     /  .'  __  '. \
$1    |  /  .'  '. |  |
$1    |  |  |   $2.'$1 /  /
$1     \  \  '-'$2 _.'$1 /
$1      '. '.___.' .'
$1        '-._
$1            '--.
"""),
 ("tux", "linux", 37, 33, r"""
$1       .---.
$1      /     \
$1      | $2o o$1 |
$1      |  $2V$1  |
$1     /  ---  \
$1    /  /   \  \
$1   |  |     |  |
$2   _\ |     | /_
$2  (___/-----\___)
"""),
]
out = ["/* Generated from original artwork; do not edit by hand. SPDX-License-Identifier: MIT */"]
for name, aliases, c1, c2, art in logos:
    lines = art.strip("\n").split("\n")
    ident = "logo_" + name
    out.append(f"static const char *const {ident}[] = {{")
    for l in lines:
        esc = l.replace("\\", "\\\\").replace('"', '\\"')
        out.append(f'    "{esc}",')
    out.append("    NULL,\n};")
out.append("static const sg_logo sg_logo_table[] = {")
for name, aliases, c1, c2, art in logos:
    out.append(f'    {{"{name}", "{aliases}", {c1}, {c2}, logo_{name}}},')
out.append("};")
open(__import__("os").path.join(__import__("os").path.dirname(__file__), "..", "src", "logo_data.inc"),"w").write("\n".join(out)+"\n")

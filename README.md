# OpenSMTPD filter-rwfrom
This is OpenSMTPD filter to rewrite "From" header. 
Rewrite rules can be configured with simple pattern matching based on MAIL and RCPT commands.

## Building
Build script will download respective OpenSMTPD-extras version and compile this filter.

```
usage: ./build.sh <filter_api_version>

filter_api_version:
   50        OpenSMTPD 5.9 and before
   51        OpenSMTPD 6.0.0 - 6.0.2
   51-head   OpenSMTPD 6.0.2 + patches
   52        after OpenSMTPD 6.0.2
```

## Configuration
Filter must be configured by editing `/etc/mail/filter-rwfrom.conf`:  

```
# - pattern will match * and ?
# - first match is applied
#

# rewrite by mail 
#mail   charlie@*   charlie@foo.bar
#mail   r??t@*      root@example.com
#mail   *@localhost me@my.domain
#mail   *           foo@bar.com

# rewrite by rcpt
#rcpt   *@foo.bar   me@foo.bar
#rcpt   foo@bar.com mii@foo.com
#rcpt   *           i@am.foo
```

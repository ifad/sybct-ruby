#! /usr/local/bin/ruby
# sample script , exec sp_who 
# ex. 
#   LANG=ja_JP.ujis ; export LANG
#   ruby  rpcsample.rb -S SYBASE -U sa -P XXXXXX 

require "sybsql.rb"

lang = ENV['LANG']
lang = 'default' unless lang

server = "SYBASE"
user = "sa"
passwd = ""
timeout = 0

while ( !ARGV.empty? )			# H.NAKAMURA patch
    case ARGV.shift
    when /^-S$/, /^-S(.*)$/
      server = $1 || ARGV.shift
    when /^-U$/, /^-U(.*)$/
      user = $1 || ARGV.shift
    when /^-P$/, /^-P(.*)$/
      passwd = $1 || ARGV.shift
    when /^-J$/, /^-J(.*)$/
      lang = $1 || ARGV.shift
    when /^-t$/, /^-t(.*)$/
      timeout = $1 || ARGV.shift
      timeout = timeout.to_i
    else
      $stderr.print "usage: ",$0,
	"  [-S server -U user -P passwd -J lang -t timeout-sec]\n"
      exit 1
    end
end

print "server=",server," user=", user,
    " passwd=",passwd," lang=",lang," timeout=",timeout,"\n"

begin
  query=SybSQL.new(
		 {'S'=>server,'U'=>user,'P'=>passwd, 'timeout' => timeout,
		     'appname'=>'rubysample','lang'=>lang}
		 )
  
  # use db
  unless( query.sql_norow("use master") ) then
    raise "failed, use master"
  end

  query.sql("exec sp_who")
  raise "failes, sp_who" unless (query.cmd_done? )

  status = query.top_status_result
  res = query.top_row_result

  print "Return Status = #{status}\n"
  print res.columns.join('|'), "\n"
  print "------------------------------------------\n"
  res.rows.each {
    |r| print "#{r.join('|')}\n"
  }
  print "OK\n"
rescue
  # raise
  # print "!! ERROR : ", $!,$@, "\n"
  print "!! ERROR : ", $!,"\n"
ensure
  query.close if(query.kind_of?( SybSQL ) );
  query = nil
end

# print "GC start\n"
# GC.start
# print "END\n"

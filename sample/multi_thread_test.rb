require "sybsql.rb"

$lang = ENV['LANG']
$lang = 'default' unless $lang

$server = "SYBASE" ; $user = "sa" ; $passwd = ""
$use_async = true

while ( !ARGV.empty? )
  case arg=ARGV.shift
  when /^-S$/, /^-S(.*)$/
    $server = $1 || ARGV.shift
  when /^-U$/, /^-U(.*)$/
    $user = $1 || ARGV.shift
  when /^-P$/, /^-P(.*)$/
    $passwd = $1 || ARGV.shift
  when /^-J$/, /^-J(.*)$/
    $lang = $1 || ARGV.shift
  when /^-noasync/
    $use_async = false
  else
    $stderr.print "usage: ",$0,
      "  [-S server -U user -P passwd -J lang -noasync]\n"
    exit 1
  end
end

def dosql(title, sql, dbname="master")
  query = nil
  begin
    query=SybSQL.new( {'S'=>$server,'U'=>$user,'P'=>$passwd,
		       'async'=> $use_async,
		       'appname'=>'rubysample','lang'=>$lang })
    query.set_strip(true)
    raise "USE DB" unless( query.sql_norow("use #{dbname}") )
    query.sql(sql){
      |cmd, status, columns, rows|
      print "[#{title}]\t", rows.join("|"),"\n"
      $stdout.flush
      nil # Not use SybResult
    }
    raise "ERROR" unless (query.cmd_done? )
    print "[#{title}]\tdosql END\n"
    $stdout.flush
  rescue
    print "[#{title}]\t", $!,"\n"
    $stdout.flush
  ensure
    query.close if(query.kind_of?( SybSQL ) );
    query = nil
  end
end

print "Async is #{$use_async}\n"

dosql("Main thread","select 'ASE version=' + @@version")
#print "[Main thread]\truby version = #{VERSION}\n" ; $>.flush
print "[Main thread]\truby version = #{RUBY_VERSION}\n" ; $>.flush

Thread.start { dosql("Thread-1","waitfor delay '0:0:05'  exec sp_help") }
Thread.start { dosql("Thread-2","waitfor delay '0:0:05'  exec sp_who") }
Thread.start {
  dosql("Thread-3","waitfor delay '0:0:05'  select name,id,type from sysobjects")
}

while TRUE
  sleep(1);
  print "[Main thread]\t#{Time.now}\n"; $>.flush
end

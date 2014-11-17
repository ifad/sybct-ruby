						     2001年 3月20日 
========
概 要
========

  本モジュールは、ruby から SyBase SQLサーバをアクセスするための拡張ラ
イブラリです。

  SyBaseサーバとのインターフェースには、サイベースクライアントライブラ
リーを使用しています。

  ただし、自分の使う範囲しか実装していません。特に、
        * 拡張エラーメッセージの処理
	* ダイナミック SQL
	* RDBC の実装
など、人によっては重要と思われる部分が欠落しています。

==========
使用条件
==========
  Ruby 本体の配布条件に従って下さい。

==========
動作条件
==========
  ruby-1.3.X 以上と、SyBase オープンクライアントライブラリーが必要です。
  
========
INSTALL
========

1. あらかじめ、ruby-1.3.X以上 と、SyBase をインストールしておいてください。

    Linux 版のSyBase について
    -----------------------------
       Linux 版 Adaptive Server Enterprise 11.0.3の場合、日本語環境で使用
      するには、RDBMS 本体 と追加モジュールファイル(locales-diff.tar.gz)をイ
      ンストールしたあと、
	      /opt/sybase/locales/locales.dat 
      の [linux] の項目に、以下のエントリーを追加してください。
	      locale = ja_JP.ujis, japanese, eucjis
              locale = ja_JP.EUC-JP, japanese, eucjis

2. アーカイブの展開

  % gzip -d <sybct-ruby-0.2.7.tar.gz | tar xvf -


3. sybct-ruby-0.2.7 ディレクトリに移動

  % cd sybct-ruby-0.2.7

4. 必要ならば、extconf.rb を修正してください。

  extconf.rb は linux + gcc + ASE12.5 + ruby1.8  用に設定しています。
  他の環境の場合は、
    sybase = "/opt/sybase-12.5"
    sybase_ocs = "#{sybase}/OCS-12_5"
    $CFLAGS $LDFLAGS  $LOCAL_LIBS 
の記述を使用する環境に合わせて変更してください。

  ---- 注意: Ruby 1.9.1 ----
    Ruby 1.9.1 から、ネイティブスレッドがサポートされましたので、リエ
  ントラントなSybaseライブラリー (***_r.soという名前)をリンクして下さ
  い。
    (例)
      Intel-32bit-Linux + ASE12.5 + ruby1.9.1 の環境なら、
      $LOCAL_LIBS = "-lct_r -lcs_r -lsybtcl_r -lcomn_r -lintl_r -rdynamic -ldl -lnsl -lm"    


5. Makefile の作成
 % ruby extconf.rb

6. make 
 % make


7. テストサンプルの実行

  すでにどこかで SyBase SQL サーバが動いていれば、以下のスクリプトで動
作確認が可能です。

    % echo $SYBASE ; echo $SYBASE_OCS  # check the $SYBASE environment variable
    % LD_LIBRARY_PATH="${SYBASE}/${SYBASE_OCS}/lib" ; export LD_LIBRARY_PATH
    % cd sample
    % ruby -I ../ ./sqlsample.rb -S dbserver -U sa -P XXXXXX 
    % ruby -I ../ ./rpcsample.rb  -S dbserver -U sa -P XXXXXX 

  dbserver には使用する SyBaseサーバの名前を指定してください。XXXXXXは 
sa ユーザのパスワードです。
    

8. インストール

  sybct.o sybct.so sybct.rb sybsql.rb を $LOAD_PATH のどこかにコピーし
てください。

  例えば、
  % su
  $ cp sybct.o sybct.so sybct.rb sybsql.rb /usr/local/lib/ruby/site_ruby/1.8/i686-linux/
です。


=======
使い方
=======
  各クラスで定義されているメソッドの詳細については、doc/index.html を
みてください。 ここでは、とりあえず使用するための簡単な説明を行ないま
す。

* フレーム *
--------------
  以下に、代表的なプログラミング手順を示しますので、参考にしてください。
  
(1) ライブラリーのロード
  require "sybsql.rb"

(2) メッセージコールバックの再定義
  必要ならば、専用のメッセージコールバックメソッドを再定義します。

  SybSQLContext クラスに、
	srvmsgCB	----	サーバメッセージコールバック
	cltmsgCB	----    クライアントメッセージコールバック
の二つのメッセージコールバック用メソッドが定義されていますので、この二
つを適当に再定義してください。
  詳細は、sybsql.rb ソースコード 及び doc/SybSQLContext.html をみてください。

  何もしなければ、デフォルトのコールバックがそのまま適用されます。

(3) サーバとの接続を確立する。
  q = SybSQL.new(hash) で 新規に SybSQLインスタンス q を作成してくださ
い。その時点で、SQLサーバとの接続が確立されます。

  hash には、サーバ名、アカウント情報、言語情報 などを指定します。
  主なキーには、

	'S' => 'サーバ名
	'U' => 'login ユーザ名'
	'P' => 'パスワード'
        'lang' => 'locales.dat に登録されてあるロケール名'
	'timeout' => 接続タイムアウト秒
        'async' => マルチスレッドアプリケーションの場合、 true に設定 
                  （省略時はfalse）

などがあります。詳細は、doc/SybSQL.html をみてください。

  接続は、q.close メソッドを発行するか、もしくは、SybSQLインスタンス
(この場合は q）がRuby によってガベージコレクションされるまで有効です。

(4) SQLの発行
  select 文のように、ローを返すSQLコマンドの発行には、SybSQL クラスの 
sqlメソッドを使ってください。
  insert, use db などのローを返さないコマンドについては、sql_norow メ
ソッドを使用した方が簡単に処理できます。

(5) SQL発行の成否確認
  sql_norow メソッドについては、その返り値が true ならば、正常処理され
ています。
  sql メソッドの結果は、直後の cmd_done? メソッドの返り値(true / false
)で判断します。

(6) 結果の取得
  sql および sql_norow メソッドの結果は、SybResult オブジェクトの配列
として、SybSQLのインスタンス変数に格納されています。

  この配列は、SybSQLのresultsメソッドで取り出すことができますが、たい
ていは、SybSQLの top_row_resultメソッドを使用します。
  top_row_result は、結果データ配列のうち、最初の通常ロー結果を返すメ
ソッドです。従って、sqlメソッドで複数のselect文を指定したり、複数結果
データを返す特殊なストアドプロシジャを指定しない限り有効です。

  SybResult が提供している、結果データをアクセスするための、主なメソッ
ドには、

  rows
	ロー結果を「配列の配列」として返します。
	[ [ロー１のデータ .... ] [ロー２のデータ .... ] ...[ローｎのデータ .... ] ]

  nthrow(nth, col=nil )
	colが省略された場合
		nth 番目の結果ロー配列を返します  [ローnth のデータ .... ]
	colが整数なら、
		nth 番目の結果ローの、colカラム目のデータを返します。
	colが文字列なら、
		nth 番目の結果ローの、'colカラム'のデータを返します。
  columns
       ロー結果のカラム情報をStringの「配列」として返します

などがあります。

(7) 接続のクローズ
  SybSQL の close メソッドの発行により、接続を切断します。
  再度接続するには、(3) に戻って新規に SybSQLオブジェクトを生成しなけ
ればなりません。


* 単純な、SQL発行プログラムの例
---------------------------------
  以下に、実際のプログラム例を示します。

      require "sybsql.rb"  # 拡張ライブラリーのロード
      begin

	# サーバ名 SYBASEに、 ユーザ sa, パスワード 'PassWord' で接続
	query=SybSQL.new( {'S'=>'SYBASE', 'U'=>'sa', 'P'=>'PassWord'} )

	# mydb データベース を使う
	raise "ERROR use mydb" unless( query.sql_norow("use mydb") )

	# SQL発行
	query.sql("select * from theTable")
	# ERROR チェック
	raise "ERROR SELECT" unless (query.cmd_done? )

	# 最初の通常ロー結果セットを res に取得
	res = query.top_row_result

	# カラム名情報のプリント  
	print res.columns.join(','), "\n"

	# ローデータのプリント
	res.rows.each {
	  |r| print r.join('|'), "\n"
	}

      rescue
	# エラー表示
	print $!,"\n"

      ensure
	# 接続を切断し、クライアントライブラリーの終了
	query.close if(query.kind_of?( SybSQL ) )
      end

=====================
サンプルについて
=====================
  sample/ ディレクトリーに 以下のサンプルスクリプトがありますので、プ
ログラミングの参考にでもしてください。

getimage.rb
------------
  Image データの検索を行なうサンプルです。
  SyBaseのサンプルデータベース pubs2 の au_pix テーブルに登録してある
全てのイメージデータを ディスクファイルに落します。
  使い方は、getimage.rb の先頭のコメントを見てください。
  
sendimage.rb
------------
  Image データの 追加／更新 を行なうサンプルです。
  SyBaseのサンプルデータベース pubs2 の au_pix テーブルに、指定したイ
メージファイルを登録します。
  使い方は、sendimage.rb の先頭のコメントを見てください。

cursor_disp.rb, cursor_update.rb
------------------------------------
  Client-Library カーソルのサンプルです。
  実行には pubs2 データベースが必要です。

isql.rb
--------
  isql コマンドのようなものです。
  実行には、ruby の readline モジュールが必要です。
  使い方は、isql.rb の先頭のコメントを見てください。

======================
動作確認されている環境
======================
  今のところ、
	OS:		Linux (Vine Linux 3.0)
	Ruby:		ruby 1.4.2 , ruby 1.6.7, ruby 1.8.2
        Sybase:		Adaptive Server Enterprise 11.0.3E6 および 12.5
の環境でしか確認していません。

======================================
sybct-ruby-0.2.11 ==> sybct-ruby-0.2.12
======================================
* Fixed compile error on Ruby 1.9.1.
  (Thanks to Bob Saveland )

======================================
sybct-ruby-0.2.10 ==> sybct-ruby-0.2.11
======================================
* applied Ruby-DBI patches from Charles.  thanks.
  - allow_serveraddr_prop.patch (this is to allow connection by
    specification of CS_SERVERADDR);
  - expose_column_types.patch (to allow introspection on column types
    of result sets); and
  - extra_defines_for_freetds.patch (these were neccessary to compile
    with freetds).

* => sybct-ruby-0.2.11_p1
  *extconf.rb 
    ASE15.0.3 Linux 64bit compile options. 
    (Thanks to Martin Jezek )

======================================
sybct-ruby-0.2.9 ==> sybct-ruby-0.2.10
======================================
* extconf.rb を変更
  * 64bit linux のコンパイルオプションを追加 
    (Thanks to Hiroyuki Sato and Kiriakos Georgiou)
  * Mac OSX(tiger) ＋ freetds(ver 0.64)  のコンパイルオプション例を追加 

=====================================
sybct-ruby-0.2.8 ==> sybct-ruby-0.2.9
=====================================
* freetds のサポート (freetds-0.64RC3 でしか動作確認していません)
  まだテスト実装の段階です。いくつかの機能は動作しません。
  詳細は英語のREADME.txtをみてください。

* README-cygwin.txt
  cygwinでの make方法を書いてみました。

==================================================
sybct-ruby-0.2.7 ==> sybct-ruby-0.2.8 の主な変更点
==================================================
* extconf.rb
  Mac OS X 10.4 用の記述 (By Scott McKenzie)

* 最新の CS_VERSION_nnn を使うように変更 ( Thanks to Will Sobel )
  

==================================================
sybct-ruby-0.2.4 ==> sybct-ruby-0.2.7 の主な変更点
==================================================
* extconf.rb
  ASE12.5 の場合の記述を追加

* SybSQL#new 'login-timeout' 属性
* SybSQL#connection_status 追加
* SybSQL#connection_dead?  追加

* いくつかのBUGらしきものを修正

=============================================
sybct-0.2.2 ==> sybct-ruby-0.2.4 の主な変更点
=============================================
* Client-Libraryレベルのカーソル処理に一部対応。
  以下の機能は未対応
    + ct_keydataを使ったカレントローの再定義
    + カーソルの再オープン
    + ホスト変数処理

* SybContext.create での async オプションの追加
  sybase API のネットワーク入出力中においても、rubyスレッドのコンテク
ストスイッチが切り替わるようにしました。

=========================================
sybct-0.2.0 ==> sybct-0.2.2 の主な変更点
=========================================

* CTlib の ct_get_data/ct_send_data を使用した Image/Textデータの転送
機能を実装。

* SybCommand.fetch を 単一ローのfetchに変更(ct_fetchにより近くした）。
  従来のSybCommand.fetch の機能は、 SybCommand.fetchloop で代替。

* SybSQL.sql メソッドに、sql{|args..|    } 形式で、１ローずつ呼び出さ
れるブロックを指定できるようにした。
  （巨大ローのフェッチと、CS_IODESC検索のため）

* 日付をいつも yyyy/mm/dd hh:mm:ss 形式でフェッチするようにした。
  （0.2.0 では localeの設定によっては、時間情報がフェッチできなかった。）

* SybReslt に トランザクションステータスとローカウントを検索するメソッ
ドを追加

=======
作者
=======

高石哲史

バグレポートなどは tetsuhumi@aa.bb-east.ne.jp  までお願いします。

===========
NO WARRANTY
===========
  THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.


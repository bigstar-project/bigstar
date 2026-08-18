# NSMB Mario vs Luigi Rollback Design Notes

> 現在の判断は直下の2026-08-18節を正とする。それ以降は、判断変更の根拠を残すための履歴であり、古い「current」「next action」を現行方針として扱わない。

## 2026-08-18 Slippi型を実用化するための制御方針とP=7初回実測

### 現時点の独立判断

Slippiの制御原則、すなわち「少量の実入力遅延を先に入れる」「訂正可能範囲だけ予測する」「上限を越える前にgame frameを止める」「欠落入力を再送する」を本forkにも採用した。前節の`D=2/P=5/H=12`を先に製品候補とする判断は撤回する。ユーザーの評価順が「Bigstar統合より先に既存scriptでP=7まで実装・試験」へ明確化され、かつ今回P=7の実訂正と安全停止を観測できたためである。現在のscript試験候補はlocal input delay `D=2`、最大連続投機tick `P=7`、ROM側history容量`H=12`とし、Bigstar GUI起動経路は変更しない。

software renderer・60fps制限ありの通常条件 `logs/codex-slippi-horizon7-practical-20260818` は人工送信遅延2-3 frameで、host/client各38訂正、最大log depth `3/2`、最大replay tick `4/3`、active平均`16.664/16.667ms`、最大`28.129/27.640ms`、33ms超0、horizon待機0、26共有heartbeat差0だった。P=7設定そのものによる通常時の性能低下はこのrunでは検出していない。

境界条件 `logs/codex-slippi-horizon7-depth-trace-20260818` は人工送信遅延8-9 frameで、hostが22訂正・最大depth 7・最大8 replay tick、clientが12訂正・最大depth 6・最大7 tickを完了した。P+1個目の実行前にhost/client `341/87`回停止して全件再開し、cannot-arm、clamp、timeout、fatal invariant、16共有heartbeat差は0だった。一方active最大は`40.373/52.958ms`、33ms超`19/6`であり、常時8-9 frame遅延を快適に処理できたとは判定しない。P=7は訂正可能な安全上限であり、そこまで常用しても滑らかという保証ではない。本forkは現在tickを実行する前に後着不一致を取り込み、そのcurrent tickもROM-loop transactionへ含めるため、7個の既実行predictionに対してlog depth 7・replay 8 tickが現れ得る。

`logs/codex-slippi-horizon7-timeout-20260818` はtimeoutだけ500msへ短縮し、入力を100 frame遅らせた。両roleとも連続確定frontierから8個目へ進む前に停止し、`500/501ms`でexit 73となった。製品候補の既定値はSlippiと同じ7秒である。

### 分離する三つの値

- `D: input delay` は物理入力を `logicalFrame + D` へ割り当てる値で、操作遅延と通常時のnetwork吸収量を決める。Bigstarの既存`2`はこの意味であり、そのまま利用できる。
- `P: prediction horizon` はremote入力が連続確定したfrontierより先へ進める最大tick数で、rollbackの製品安全域を決める。ユーザー設定の`InputMaxFrameLead`やROM history容量とは別にする。
- `H: checkpoint/history capacity` は実装上保持できる履歴量である。常に`H > P`を満たし、現行12 entryを維持する。

rollback有効時の現行`InputMaxFrameLead`は、future labelである`sendFrame`と「最大受信frame」を比較しており、欠落穴を飛び越せるうえ、prediction horizonとclock pacingを混同している。rollback経路ではこの値を安全上限として使わず、新しい`P`によるgateへ置き換える。非rollback経路のlockstep調整としては残せる。手動launcherの`RollbackMaxResimFrames=7`もROM-loopでは実際には無視され、固定history容量由来の11へ置き換えられているため、実効設定として扱わず`P`へ統合する。

### 安全契約

1. generation開始時にinput delay分のneutralを確定済みとしてprimeし、`highest contiguous confirmed remote frame`を明示的に持つ。packet `100,102`を受け取ってもfrontierは100のままで、101が来た時だけ102まで進める。現行`LastReceivedInputFrame=max(received)`はtelemetryには使えても安全判定には使わない。
2. logical frame `F`を実行する直前に `F - contiguousFrontier <= P` を検査する。範囲内だけstale-repeat予測を許し、`P+1`個目へ入る前はemulation、描画、audio timelineを進めずnetwork pumpと再送だけを行う。入力到着後は同じframeから再開する。
3. 予測値は「packet到着順で最後に確認した入力」ではなく、対象frameより前で最も新しい確定入力から選ぶ。現行`PredictionRuntime::Confirm()`は再順序化した古いpacketでも`LastConfirmedInput_`を上書きするため、real-WAN gateより前にframe順の探索へ直す。
4. horizon gateが正しければ、後着不一致は必ず保持history内にある。現行のdepth 11超 `ClampResimulationMismatch()`は古い不一致を捨てて最近のframeだけ直すため削除し、範囲外不一致やcheckpoint欠落は同期継続不能の明示的なfatal invariant errorとする。無言でplayを続けない。
5. script経路は`InputBundleHistory=11`、すなわちcurrentを含む最大12 entryへ増やし、stall中は最新bundleを50ms間隔でreliable resendする。これで今回のdelay試験は回復したが、Slippi同様の明示的なcontiguous ACKと未ACK列はまだwire protocolへ入れておらず、packet loss試験より前の残作業である。新しいgame inputを生成してhorizonを押し広げることはしない。
6. horizonでの連続停止は7秒でexit 73にする。通常の`RollbackInputWaitUs`は0を既定にし、毎frameの短い待ちと安全上限でのhard waitを混同しない。GUI表示はBigstar統合を再開するときの別作業とする。
7. `D/P`、protocol version、ROM-loop backend/history契約を開始handshakeとruntime identityへ含め、peer間不一致は対戦開始前に拒否する。再戦generation resetではcontiguous frontier、ACK、prediction、pending rollbackを同時に初期化する。

### 実装と昇格の順序

1. 完了: ROM不要testで、連続frontier、穴あき・逆順packet、generation reset、P=7と8個目の区別を固定した。arrival-order依存のstale predictionをframe順探索へ変更し、P有効時のROM-loop事後clampをfatal invariantへ置き換えた。
2. 完了: manual `-SlippiRollback`を`D=2/P=7/H=12`、software renderer、旧`InputMaxFrameLead`無効、7秒timeoutへ揃えた。通常2-3 frame条件、深度7を発生させる8-9 frame条件、短縮timeoutを実ROMで通した。
3. 次は固定12-entry bundleだけでなくcontiguous ACK/未ACK再送をprotocolへ加え、loss・重複・再順序化・短時間断をscriptで試す。`D=0/1/2`も同じnetwork条件でA/Bするが、一つのrun中に動的変更はしない。
4. P=7の合格条件は、cannot-arm/cap/fatal invariantが0、継続heartbeat差0、通常network条件で対戦中実効`59.5fps`以上、rollback起因の33ms超連続frameが0、訂正区間後のpipe/player/OAM画像差が0、死亡・復帰・土管・item・再戦を通過、停止後の復帰または理由付き終了とする。単発最大値、訂正深度分布、audio underrunも別々に記録する。今回の通常短時間runは状態・性能を通ったが、画像・音声・event・長時間手動gateは未完である。
5. ユーザー指示により、Bigstarの現行`coredelta`起動設定とGUIはこの段階では変更しない。P=7のscript検証を先に続ける。

## 2026-08-18 Slippi/Tangoの遅延・深度制御再確認

### Slippiの現行ソース上の契約

2026-06-12のmainline Slippi Dolphin `2b29463b82f32c41de6c9af94f42481174ba9979`、stable Ishiiruka `e7711b104b339a99385f2bb12b472d46140a7bc7`、2026-05-01のSSBM ASM `fcf47f10dc244152c2ebaa3a9dec142ea42243b7`を照合した。mainlineとstableの双方で`ROLLBACK_MAX_FRAMES`は`7`、ASM側の`ROLLBACK_MAX_FRAME_COUNT`も`7`である。Dolphinは7個のsavestate objectを用意し、remote inputも最大7件をgame側へ返す。ASMのlocal/predicted input ringは稀なindex越境への余裕として14件だが、これは対応rollback深度を14へ増やすものではない。

上限は単なるallocation数ではなく実行時に強制される。各remote playerについて `latestRemoteFrame >= currentFrame - 7` を満たさなければ、Dolphinは `Halting for one frame due to rollback limit` としてgame frameを進めず、未ACK local inputだけを再送する。停止が7秒、すなわち60Hzで420回を超えると該当playerを切断する。したがってSlippiは7を超えて予測してから古い訂正を捨てるのではなく、訂正可能範囲を出る前にlockstep待機へ移る。

予測は最新remote padのstale repeatである。ASMは最初の未着frameからpredictionを記録し、入力到着後にactualと比較し、複数playerなら最も早い不一致frameへsavestate loadする。catch-upはgame engineを再ループし、通常の表示frameを余分に提示しない。prediction中はframeごとのsavestateをcaptureし、Dolphin側は7 slotを古い順に再利用する。packetには最新入力だけでなく未ACK入力列も含め、欠落回復を次packetへ重複させる。

入力遅延は実在し、既定値・公式推奨とも2 frameである。最初にdelay件のneutral padを送り、実際のlocal padを `frame + delay` 番へ割り当てるため、表示上だけの設定ではなくlocal inputの適用を将来へずらす。現行UIは1-9、ASMの防御clampは1-15で、FAQは2 frameを130ms pingまで推奨し、playerごとに異なる値でも接続可能としている。これはSlippiがMelee固有の表示遅延を約1.5 frame削減したうえで2 frameを戻す設計理由を含むため、NSMBへ同じ既定値を無条件に移す根拠にはならない。

Slippiの「最大7 frame」は7個の投機tickを含めた呼称である。例えばremote確定が93、currentが100なら投機対象は94..100の7 tickだが、最初と最後のframe番号差は6になる。本forkのログが使う `depth = current - restoreFrame` と数値を比較するときは、このinclusive/exclusive差を明記する。

### Tangoとの比較と本forkへの含意

現行Tango `259eafbb09ef65ad2431548bb093619e51016bab` も既定値は2 frameで、ユーザーの記憶した数字は確認できた。ただし現在の実装ではSlippi型のinput scheduling delayではなく、netcode frontierより表示coreを2 tick後ろへ置くpure local `present delay`である。範囲は0-10で、RTTから片道frame数+1を提案できる。入力frontier自体は先へ進むため、同じ「2」でもSlippiの `input(frame) -> apply(frame+2)` とは別の設計である。

Bigstarがrollback有効化時に設定する既存の2 frameはSlippi型である。UIの`rollbackInputDelayFrames=2`はTauriから`MELONDS_NSML_DELAY=2`へ渡り、runtimeは現在読んだlocal inputを `logicalFrame + delay` 番へ保存・送信し、現在のlogical frameには履歴中の入力、開始直後はneutralを適用する。表示coreやframebufferだけをsimulation frontierより後ろに置く経路はない。Slippiとの差は、Bigstarでは0-16を選べるroom共有設定であり、playerごとのmixed delayではない点である。またこれは入力遅延の分類であり、Bigstarの現行`rollback_enabled`起動設定自体は`coredelta` backendを選んでいるため、GUI経路全体が現在のROM-loop候補へ切り替わっているという意味ではない。

本forkで採るべき次の順序は、(1) 12-entry historyを「深度11まで常用可能」という意味にせず、実測済みの訂正性能・表示gate内に製品prediction horizonを置く、(2) oldest contiguous confirmed remote frameを基準に、履歴外訂正が起きる前に安全に待機する、(3) local input delay 0/1/2を同一人工RTT・jitterでA/Bし、rollback深度・outer hitch・操作感を別々に評価する、である。Slippiの7は方式の参考値であり、DS二台分相当の再演算コストを持つ本forkが7を快適に処理できる証拠ではない。現状のdepth 11超clampは安全fallbackではないため、実WAN相当試験より先に停止境界を実装・検証する。

## 2026-08-18 render processを含むROM-loop訂正

### 再戦generation timelineの修正

再戦ずれについて、旧「旧generationのframe 934がprediction timelineへ残った」という判断はログとframe変換経路の再確認により撤回する。bootstrap checkpointを復元するとNDS/inputの論理時刻はframe 934付近へ戻る一方、manual/test harnessの外側frameは13051付近まで単調増加する。packet JIT scratchは既に `NetplaySession::LogicalFrame()` で前者へ変換していたため、再戦後のmismatch 934は新generationで正しい値だった。ROM-loopのcheckpoint保存・訂正だけが外側current 13051を受け取り、異なる座標系を引いてdepth 12117にしていたことが直接原因である。

`BeforeRunFrame()` でROM-loopへ渡すframeをgeneration-local logical frameへ統一した。generation resetでは予測mapだけでなく、pending rollback frame、pending observed frame、前generationのlast confirmed inputも消去する。統計counterは長時間runの診断に必要なので累積のまま保持する。unit testではreset後にpendingがなく、最初の未確認remote inputが前試合の最終入力ではなくneutralから予測される契約を固定した。

frame limiterなし10000-frame run `logs/codex-rematch-logical-frame-fix-20260818` は2回の再戦を行って3試合目まで進み、host/client `38/52` correction、cannot-arm・failed/capped correctionゼロ、152共有heartbeat差ゼロだった。logical checkpoint offsetはgenerationごとに `0:241,1:235,2:238` でそれぞれ一定である。offsetがgeneration間で異なるのはrestore後の起動境界差で正常なため、解析器もgeneration resetを読み、同一generation内のdriftだけをrollback failureにする。

software renderer・frame limiterありの対応run `logs/codex-rematch-logical-frame-fix-paced-20260818` も同じ2再戦と152共有heartbeat差ゼロを再現した。host/client correctionは`81/10`、cannot-arm・failed/capped correctionゼロ、active平均`16.722/16.726ms`、実効`59.80/59.79fps`だった。外側最大`155.527/153.515ms`は最初の再戦checkpoint復元frameであり、2回目は`127.530/133.447ms`だった。stage actorが有効になった直後にも両role同じframeで`40-53ms`級のJIT/scene初期化hitchがある。論理時刻の修正自体に持続的な性能低下は検出していないが、再戦を無停止にできたとは扱わない。

再戦timelineは自動correctness gateに続いて手動gateも通過した。ユーザー手動run `logs/nsmb-mvl-manual-local-20260818-180655` は2回再戦しても目視上のずれがなく、両roleのgeneration 1・2 resetをログでも確認した。cannot-arm・failed/capped correctionはゼロだった。176共有heartbeat中の2標本に一時差があり、significant object差はframe 14760の1標本だけだったが、次の14880標本では再一致したため継続ずれではない。このrunは両方向の送信を2または3 frame待たせる人工遅延を有効にしている。実WANの輻輳、packet再順序化、帯域制約、切断まで再現した結果ではない。

次は人工送信遅延を段階的に増やし、rollback深度分布、cannot-arm、継続state差、outer frame時間の安全域を測る。約0.15秒のcheckpoint復元とstage開始直後hitchも別のperformance blockerとして削減する。死亡/復帰、土管遷移、item、2D OAM、音声、depth 11超fallbackは引き続き未検証である。

### 長時間手動runの同期trace停止

`logs/nsmb-mvl-manual-local-20260818-141625` では、一度の目視上の長時間停止とその後のずれが報告された。outer計測はhost最大`1249.975ms`、client最大`1173.334ms`で、通常のROM-loop transactionの数十ms級とは別の異常だった。

frame 9386-9392を両roleで照合すると、hostは訂正完了traceを含むafter-frame先頭で`221.981ms`、network pump内のinput traceで`410.519ms`を消費した。hostの入力が止まった結果、clientはlead 9でframe-lead throttleへ入り`783.796ms`待ち、clientのbefore-hook全体は`1090.577ms`になった。frame 13200-13201でもgameplay heartbeat、rollback stats、phase traceの同期flushが両roleで`122-533ms`を占めた。`FinalizeNSMLGameRAMRollbackTransaction()`自体はcontrol wordをclearするだけであり、ログなしの通常回は同じ区間が数十µsなので、ROM catch-upや3D破棄の必然的な負荷ではない。

原因は `-InputNetplayTrace` / `-PerfBreakdown` が有効な実運用診断で、emulation threadが `printf` 後に毎回 `fflush(stdout)` していたことだった。redirect先のfile writeが遅い一方のpeerをframe-lead throttleが相手へ伝播し、ローカル2-process全体の停止へ増幅した。手動runのfirst significant object diffはframe 7080で停止frame 9392より前であり、このログだけから停止を恒久ずれの原因とは判断しない。

runtime trace用に`NsmbTraceOutput`を追加した。producerは整形済み1行を最大4MiBのbounded queueへ積むだけで、Windowsではbelow-normal priorityのwriter threadが2ms単位でbatchし、stdout flushを最大1秒間隔に制限する。heartbeat、rollback mismatch/arm/complete/stat/error、input send/recv/throttle、packet bridge詳細、performance phase、60-frame JIT scratch traceを移行し、終了時だけ同期drainする。queue満杯時はemulationを待たせず行をdropして件数を報告する。`MELONDS_NSML_SYNC_TRACE_OUTPUT=1` は同一バイナリA/B用の旧同期経路である。

同一seed `0x111AF455`・同一入力・frame limiterなし5400-frame A/Bは次のとおりで、asyncの平均差は約`0.05ms`（`0.7%`）だった。

| Trace output | Host average/max | Client average/max | State gate |
| --- | ---: | ---: | --- |
| synchronous control | `7.564/32.043ms` | `7.562/35.140ms` | 38 heartbeat差分0 |
| async candidate | `7.614/34.563ms` | `7.613/36.602ms` | 38 heartbeat差分0 |

最終の60fps、software renderer、14000-frame完全入力再生 `logs/codex-async-trace-final-paced-14000-20260818` は、host/client `1027/1029` correctionを全件完了し、cannot-arm、failed/capped correction、trace dropはゼロだった。active outer平均は両role`16.666ms`、最大`38.562/33.851ms`、連続slow frame最大1。旧停止地点9392と13200を越え、秒単位停止は再現しなかった。109共有heartbeat中、frame 3240だけobject生成位相が一標本ずれたが後続で再一致し、継続差は0だった。

この自動gate後、ユーザーは同じ手動経路を2回実行した。`logs/nsmb-mvl-manual-local-20260818-151515` はframe 7560までhost/client訂正`573/579`件、共有heartbeat 56標本差ゼロ、outer最大`33.285/34.941ms`だった。長い `logs/nsmb-mvl-manual-local-20260818-151729` はframe 20280まで訂正`1686/1690`件、共有heartbeat 162標本のsignificant object差とactive count差がともにゼロ、cannot-arm・failed/capped correction・trace dropもゼロだった。outer最大は`36.281/31.526ms`、連続slow frame最大`1/0`で、秒単位停止は再発せず、ユーザー目視でもずれなしだった。解析器の`status=failed`は手動終了でframe limit行がないためで、emulation abortではない。これにより初戦同期と同期trace停止修正は手動gateを通過した。土管Xぶれの消失自体は今回の報告で明示されていないため、presentationの確認項目として残す。再戦generation timelineは上の節に記した自動2遷移gateまで完了している。

完全記録済み手動入力 `logs/nsmb-mvl-manual-local-20260818-002906/recorded-inputs` とseed `0xC6D26F70` により、手動の恒久差を自動再現した。開始barrier無効化とshared epoch未確定時の入力gateという `main` 統合回帰を先に除去した後も、frame 3487の単発depth 4訂正はframe 3517のmoving hazard生成でlockstepから分岐した。

原因は、中間catch-up tickでNSMBのrender process listを省略していたことだった。これはGPUへの描画だけでなくgame object側のrender callbackも丸ごと省き、tested eventでは将来のmoving hazard/object状態へ副作用を残した。中間tickでもgame render processを実行する診断flagを有効にすると、単発3487訂正と近接3訂正の両方でhost/clientの比較対象game-state fieldがlockstepへ完全一致した。手動launcherではこのflagをROM-loopの既定とする。font updateの中間省略は維持している。

5400-frameの完全入力stressはhost 346、client 330 transactionを全件完了し、cannot-armなし。frame limiterなし・software rendererで平均 `4.937/4.945ms`、最大 `26.241/25.595ms`、`33ms`超ゼロだった。normal controlとの画像は訂正直後frame 3494だけ差があり、frame 3500で両roleとも全画素一致した。600-frame間隔の長時間比較ではhostは全採取点一致、clientは投機中の1800/2400だけ差があり3000以降再一致した。従って恒久的な描画破損は検出していないが、訂正直後の短い表示補正と単発26ms spikeは残る。

現時点の製品候補は、Main RAMのSDK runtime除外、checkpoint suffix無効化・中間再構築、12-entry history、開始barrier、そして全中間game render process実行を組み合わせたROM-loop経路である。SDK runtime全復元はgame loopを停止し、部分復元も反復runで恒久差を直さなかったため採用しない。

開始barrierを既定化した際、ローカル手動launcherのseed自動受信と `DeferNetworkUntilStart` が矛盾した。clientはゲーム起動前にseedを10秒待つが、hostはframe 870までnetworkをpumpしないため受信できず、clientがzero seedのまま開始状態を構築して `seed-mismatch` になった。ローカル2-process起動ではlauncherが共通seedを生成して両roleへ事前設定する。外部hostと組み合わせる `ClientOnly` では受信方式を維持する。`logs/codex-manual-auto-seed-startup-fix` のframe-limitあり・software renderer・seed未指定試験はframe 870で開始同期を承認し、1000 framesを正常終了した。

`logs/nsmb-mvl-manual-local-20260818-132459` の初戦は目視上の恒久ずれを再発しなかったが、character・地形、とくにLuigi側で土管のX座標が周期的にぶれた。同一seed・完全入力を毎frame保存した `logs/codex-client-pipe-x-jitter-replay-20260818` でこの形を再現した。Luigi画面の右側pipe領域は通常frameの暗緑連結画素数1435に対し、訂正直後1127が1947、1129が1755へ増えた。左端はcameraに従い毎frame `-2/-3px` で動くのに、訂正直後だけ右端が逆へ伸びるためcamera jitterではなく、異なるreplay tickのpipe geometryを同じdisplay frameへ重複submitしていた。

最初に中間render callback中の全3D register/FIFO writeを捨てる案を試した。pipe領域は全frame 1435へ戻ったが、完全入力5400-frame stressでframe 3360以降に6回のobject/hazard差を生んだ。render callbackだけでなく、行列、geometry test、FIFO timingまで消すためであり、state correctness条件に反するので棄却した。

採用案は、guest callbackと全FIFO entryを通常どおり実行しながら、中間sceneだけをFIFO順序付き内部markerで囲む。begin marker実行時にgeometry builder境界を保存し、end markerでvertex/polygon countを戻す。中間sceneは表示されないためvertex transform、clip、polygon RAM storeだけを省略するが、matrix/test/FIFO commandとpipeline timingは維持する。最終replay tickはmarkerで囲まず、通常sceneだけがVBlankで提示される。手動launcherでは `-SlippiRollback` / `-RomLoopRollback` の既定で有効とし、診断用 `-RomLoopNoIntermediate3DDiscard` で無効化できる。

pipe gate `logs/codex-client-pipe-x-jitter-fast-discard-20260818` はframes 1125-1130の暗緑連結画素数がすべて1435で、二重geometryを消した。完全入力stress `logs/codex-intermediate-fast-discard-stress-20260818` はhost/client 354/333 transaction、cannot-arm・failed/capped resimulationゼロ、38個の共有heartbeat標本でobject差ゼロだった。同一現行バイナリのframe limiterなしA/Bは、採用案が平均 `6.793/6.798ms`、最大 `31.372/28.961ms`、無効対照が平均 `7.866/7.878ms`、最大 `45.876/35.671ms` であり、表示修正は約1.08ms/frameの性能改善にもなった。paced 5400-frame runは平均 `16.665ms`、最大 `34.596/35.254ms`、連続slow frame最大1、object差ゼロである。単発 `33ms`超はhost 3/client 1なので完全な無hitchは未達だが、持続的60fps低下は検出していない。

再戦経路の旧depth 12117問題は、上のgeneration timeline修正で解消した。旧frame 934は残留値ではなく新generationの正しいlogical frameだったため、この段落にあった旧診断は現行判断から除外する。初戦の同期と秒単位停止は実手動目視・内部heartbeatの双方で通り、再戦も自動2遷移gateと手動2再戦gateを通った。pipeの自動再現も通った一方、今回までの手動報告は土管Xぶれの消失を明示しておらず、旧3494/3500 image control、2D OAMだけのちらつきも未確認なので、3D以外を含む全presentation完了とは扱わない。死亡/復帰、土管遷移、item、音声、depth 11超fallbackも未検証として残る。

## 2026-08-04 Slippi-style game-memory restore

### 現在のlive実装と境界

`RollbackBackend::RomLoop`を実late-input timelineへ接続した。`WritePacketBridgeJitScratchIfNeeded()`がnetwork pump、送信、remote prediction/confirmation、現在frameのlocal input記録を終えた直後にだけROM-loop訂正を予約する。これにより、訂正historyにはcheckpoint frameから現在frameまでの両player入力とpacket tickが揃い、固定診断triggerを使わない。

両peerを独立に短間隔操作する `tests/nsmb_us_direct_mvl_romloop_bidirectional_stress.inputs` により、旧版の画面停止をboundedに再現してSDK runtime領域の誤restoreを修正した。しかし、2026-08-04の手動run `logs/nsmb-mvl-manual-local-20260804-013956` は停止しない一方で序盤から状態が一致しなかった。ログを訂正単位で比較すると、初期の `checkpoint - gameFrame` は両側 `241` なのに、訂正完了ごとに1ずつ増え、終了付近はhost `1459`、client `1226` だった。両peerの訂正回数が違うため、この1 tick欠落が恒久的なsimulation時刻差になっていた。

LCD defer無効化は同じ入力で停止し、ARM9 register/DTCMをcheckpointへ追加しても停止した。一方、history loopを維持してMain RAM copyだけ省いたcontrolは57 transactionを完走した。復元prefixの二分では `0x02094600` まで継続し、`0x02094700` を含めると最終tick後に通常game loopへ戻れなかった。この256-byte境界は `symbols9.x` の `OSi_UseTick=0x020945A4`、`OSi_UseAlarm=0x020945B4`、`OSi_AlarmQueue=0x020945B8` と一致する。

checkpointとrestoreは同じguest control pointでなければならない。最初のlive版はfrontendの外側frame境界でMain RAMを保存し、固定PC `0x02004EC8`で復元したため、実訂正後にARM9 prefetch abortを再現した。この失敗により、診断PoCの短いsemantic一致だけではproduction境界を証明できないと判明した。

主因はMain RAM全体をgame-owned stateとして戻したことだった。NitroSDKのthread/tick/alarm/filesystem/WM stateは現在のCPU・scheduler・peripheral timelineに属し、過去game memoryと同時に復元できない。現行版はcheckpointの4 MiBを保持するが、restore時は `0x020942A0..0x02097FFF` のSDK runtimeと `0x02001AC0` からのROM-loop controlを現在値のまま残す。DTCMとCPU registerは現在値を維持する。

加えて、一つのtransaction中は次のlate mismatchをpendingのまま保持してhistory上書きを防ぎ、restore gate時点の実game-frame差へhistory countを合わせる。restore gateは現在tickの実行前なのでhistory範囲は両端を含み、countは `preRestoreGameFrame - historyStartFrame + 1` が正しい。旧式の `+1` なしは訂正ごとに現在tickを捨てていた。履歴容量も8件ではlead 8時の実checkpoint差を覆えず、旧手動runのhostで25件の `cannot arm` を生んだため、注入input gate直前まで使える12件へ拡張した。履歴消費後の次gate到達を明示的な完了境界とし、after-frameでcontrolをclearする。ROM wrapperは入力・loop counter・font・renderの元BL return addressをLRへ復元してから戻る。

修正後のneutral-tail run `logs/romloop-live-timeline-capacity12-neutral-tail-20260804-run1` は両peerとも121 mismatchを121回arm/completedし、cannot-arm、schedule failure、abort、freezeはゼロだった。全armed recordの `checkpoint - gameFrame` はhost/clientとも最初から最後まで `241` で一定である。入力変化終了後のframes 1820/1840/1880/1920/2000/2100/2200では両player座標・速度、moving hazard座標・速度、active object数が一致した。software rendererかつ描画ありで `59.90/59.89fps`、outer max `24.961/29.005ms`、`33ms`超ゼロだった。

このneutral-tailだけでは不足だった。次の手動run `logs/nsmb-mvl-manual-local-20260804-020633` は時刻offset `241`一定・cannot-armゼロでも、frame 960の一致から1080のplayer差、1680のobject/hazard差へ進み、3000以降のneutral tailでも座標差が残った。手動ログのmismatch列から復元した `tests/nsmb_us_direct_mvl_romloop_manual_020633_prefix.inputs` は、修正前にframe 1101以降のほぼ連続差を自動再現した。

原因は訂正済みcheckpoint ringの管理だった。ROM-loopは過去RAMを再演算してもrestore地点より新しいsnapshotを残していたため、近接する次のmismatchが訂正前prediction入りsnapshotを選び、前の誤予測を復活させた。full-frame経路の `Store.EraseAfter(restoreFrame)` に相当する処理が欠けていた。現行版は各snapshotにvalidityを持たせ、restore地点より後を無効化し、ROM catch-up中の各guest input gateで訂正済み中間checkpointを同じdisplay frame番号へ再構築する。suffix無効化だけでは4連続入力で12-entry上限を超えたため、中間再構築は correctness requirement であり省略しない。ROM-loopのclampは容量に合わせて最大depth 11とし、旧設定7での不要な切り捨てを避ける。depth 11超のstall/fallbackは未実装なので、clampが一度でも出たrunをanalyzerで `rollback-fail` とする。

最終コードの `logs/romloop-manual-020633-final-verified-run1` はhost/client 24/25 correction、cannot-arm・depth clampゼロ、offset `241`一定で、差を各未確定prediction区間だけに限定し、各訂正後に再一致した。no-trace反復 `logs/romloop-manual-020633-prefix-rebuild-perf-run1` は `59.54/59.57fps`、outer max `23.281/25.197ms`、`33ms`超ゼロ。密な121 correction/peerの `logs/romloop-checkpoint-rebuild-stress-perf-run1` は `59.73/59.71fps` だが、outer max `30.535/45.868ms`、client `33ms`超2回である。未確定境界だけのsnapshot保存は再び恒久差を出し、restore staging buffer再利用は約0.37秒spikeを出したため、どちらも棄却した。

手動launcherは`-SlippiRollback`または`-RomLoopRollback`でこのbackendを選ぶ。game-tick patch済みhost/client ROMを別名とmanifestで生成し、checkpoint interval 1、window 16、ROM-loop実効最大depth 11、input delay 0、既定send delay 2/jitter 1、JIT exact chain/self-loopを設定する。bootstrap入力終了後は各melonDS windowの物理入力がそのままnetplay timelineへ入る。実行系スクリプトはrollback用途に限らずsoftware rendererを既定とし、OpenGLを明示的に選ばない限り `Screen.UseGL=false`、3D renderer 0で起動する。

現段階はbounded live PoCのままである。最新手動run `logs/nsmb-mvl-manual-local-20260804-034019` は、offset `241`一定、cannot-arm/depth clampゼロ、全transaction完了でも、frame 3600からplayer/object/hazardが分岐し、最後の訂正完了後4080-4320にもplayer座標差が残った。従って時刻drift、history不足、stale checkpoint再利用の三原因を直してもcorrectnessは未達である。frame 1320/2640/3480の一時差は後続訂正で再一致しており、永続化境界は3600付近のobject/event更新に絞られる。

旧手動launcherは物理入力recordがopt-inだった。mismatchのactual列だけから作る入力は、予測前に届いたtransitionを含まない。最新runの60-frame `InputNetplay` sampleと照合すると、部分復元列はplayer 0で21点、player 1で24点が実状態と異なる。同じmatch seedと部分復元入力を使い、role別send delayでdepth 1-8も作った自動runは再一致したが、元runとはframe 3360のplayer座標・object列が別ルートなので反証にならない。今後は手動launcherの `RecordInput` を既定有効にして各roleの完全入力を保存し、非対称delivery再現用にrole別send delayも指定可能とした。mismatch列からのexporterはpartial reconstructionであることを出力内とwarningで明示する。

現在のblockerは、最新runそのものをexact replayできる入力がないことである。Main RAMから除外したSDK runtime、DTCM/CPU/scheduler、event/RNGなどは候補に留まり、どれかを原因と断定する証拠はない。次の完全記録付き手動runで恒久差を自動再現し、分岐直前checkpointからnormal tickとROM replayのRAM/RNG/object lifecycleを比較して最初の異なるwrite/callを探す。correctnessが通るまでは単発 `45.868ms` の性能最適化、描画・音声、長時間production gateへ進めない。

### 診断PoCの根拠（live接続前）

Slippi型ROM-loopを本線として継続する。診断用の4 MiB Main RAM ringから過去game memoryだけを復元し、CPU register、GPU、scheduler、ARM7/peripheral timelineは現在のまま保つ経路を接続した。復元後はROM入力gateで過去入力から現在入力までを連続実行し、最終tickだけ通常描画する。depth 1なら2 tick、depth 2なら3 tickを一つのdisplay frameで処理する。

最初の実装は`NDS::RunFrame()`外側境界でRAMを復元した。game counterはtarget/controlで一致したが、moving hazardの`+0x60/+0x70`座標が訂正直後・回復後とも1 tick遅れた。stage traceは復元直後のguest tickがinput/delete/create/gameplay stageを通らず、外側frame途中のARM9 PCを保ったまま古いRAMへ差し替えたことが原因だと示した。この案は描画・当たり判定を壊し得るため棄却した。

復元点をROM loop先頭の固定PC `0x02004EC8`へ変更した。JITはこの分岐を同一blockへ取り込むため通常dispatcher hookでは捕捉できず、診断フラグ有効時だけ該当guest instruction直前へhost callbackを生成する。history control/tableだけは現在値を退避してRAM復元後に戻し、古いgame frameから`depth + current`件の入力を時系列順に適用する。この境界変更後、depth 1とdepth 2は訂正直後・通常回復後とも172 semantic field差分0となり、以前ずれたmoving hazardも一致した。

| Depth | Correctness evidence | ROM transaction | dumpなしouter max host/client | 判定 |
| ---: | --- | ---: | ---: | --- |
| 1 | `logs/slippi-game-ram-direct-ring-depth1-semantic-run1`、直後/回復後172 field差分0 | `3.169/3.264ms` | `19.200/18.472ms` | 短区間state gate pass |
| 2 | `logs/slippi-game-ram-jit-instruction-depth2-run1`、直後/回復後172 field差分0 | `4.200/3.727ms` | `19.720/19.356ms` | 短区間state/perf gate pass |

depth 1の最終測定では毎frame 4 MiB save最大`0.422/0.328ms`、restore `0.213/0.224ms`、90-frame平均`16.665/16.662ms`、`25/33ms`超ゼロだった。host最大`19.200ms`はframe 1079で、訂正frame 1002ではない。depth 2値は訂正時にringから別bufferへ4 MiBを一度余計にcopyしていた版であり、そのcopyは後に削除した。厳密な`16.667ms`最大値はまだ満たさないため、訂正frameに約2-3msの局所超過が起こり得るという評価を維持する。一方、25ms級の目立つ停止は今回の単発測定では発生していない。

software rendererでtarget roleを入れ替えた`logs/slippi-game-ram-visual-depth2-{host,client}-target-run1`では、display frame 1002の同一role PNGがhost/clientともSHA-256まで一致した。両runとも訂正直後・回復後の172 field差分は0である。これはtested movement routeの短区間presentation passであり、イベント全般や長時間の描画完全性を証明しない。

この時点の実装は、診断関数内static ringと一回の固定triggerだけを持つPoCだった。上のlive実装でper-`NDS` gate ringと実prediction/confirmationへの接続までは完了したため、この段落の「未接続」と次actionは履歴としてのみ残す。既存full-frameは引き続き比較baseline/fallbackとして保持する。

## 2026-08-02 post-hotfix Slippi-style ROM-loop remeasurement（game-memory restore前の履歴）

### 結論

`ARMv5::JumpTo()` hot-path regression修正後に同じguest-owned/JIT ROM loopを再測定した結果、旧「ROM loopはdepth 1でもfull-frame replayより遅く、depth 2から60fps予算を満たさない」という性能判定は撤回する。診断無効時にも全ARM9 jumpで`getenv()`していた回帰が、ROM内で高頻度に分岐するcatch-up loopを特に強く汚染していた。

frame limiterなし、描画なし、JIT、host/client別CPU affinity、exact block chain/self-loop、同一履歴入力というkernel測定は次のとおりだった。depth 1-2は3回ずつ、各6 role sample、depth 4/7は各1回、各2 role sampleである。

| Depth | 修正後ROM-loop kernel | 修正前の保持値 | Input hash / ticks | 判定 |
| ---: | ---: | ---: | --- | --- |
| 1 | `1.871-2.199ms` | `12.624-14.470ms` | 1 tick | 旧値から約6倍改善 |
| 2 | `1.983-2.987ms` | `16.720-28.513ms` | `A23040D5` / 2 ticks | kernel性能gate pass |
| 4 | `2.622-2.700ms` | 旧経路は表示intervalへspill | `7C3924A2` / 4 ticks | kernelのみpass |
| 7 | `3.123-3.428ms` | `34.341-39.657ms`級 | `1A24E475` / 7 ticks | kernelのみpass |

固定60fps、`InputMaxFrameLead=8`、通常current frameを含むouter active-frame timerでも、一回の強制transactionは次の範囲に収まった。dormant probeの同条件controlはouter max `18.520/18.335ms`だった。

| Depth | ROM transaction | Outer max host/client | `25ms`超 | `33ms`超 |
| ---: | ---: | ---: | ---: | ---: |
| 1 | `2.942-3.405ms` | `22.121/22.112ms`（2 run中の各role最大） | 0 | 0 |
| 2 | `5.755-6.107ms` | `21.970/22.138ms`（2 run中の各role最大） | 0 | 0 |
| 7 | `9.083-9.573ms` | `24.314/23.710ms` | 0 | 0 |

depth 2はsoftware rendererでhost-target/client-targetを入れ替えた二つのpaced A/Bも行った。172 semantic fieldは訂正直後と通常回復後の双方で差分ゼロだった。同一roleのtarget/control PNGはhost、clientともSHA-256まで一致した。cross-peer curated RAMにはrole固有・render-local領域を含む差が残るため、full RAM exact一致を主張するものではない。

### 比較上の制約

この結果はSlippi型のgame-tick kernelとcorrected endpointが有望だと示すが、既存full-machine rollbackよりproduction end-to-endで確実に速いとはまだ言えない。現在の診断transactionには、過去checkpointのrestore、訂正後checkpointの再保存、live prediction/confirmation timeline、SPU restore/output処理が接続されていない。既存depth 2 full-frameのrestore p95 `3.346ms`とintermediate save p95 `1.499ms`を単純に足すだけでも、今回の約`3ms`のouter差は消え得る。またpaced測定は各run一回の強制transactionで、既存full-frame depth 2の9訂正/roleとはsample数が異なる。

ユーザー方針により、以後はSlippi型ROM-loopをrollbackの本線として最適化する。これは未完成経路を完成扱いする判断ではなく、深度増加時のscalingを優先して研究・製品化対象を一本化する判断である。既存full-frameは比較baselineと、ROM-loopが性能・画像gateを失敗した場合のdepth 1-2 fallbackとして保持する。

毎frameの実`tinycorepreimage` checkpoint保存を有効にし、full-frame resimulationだけを無効にした追加gateも行った。これによりROM transaction外の通常checkpoint保存がouter timerへ含まれる。restoreはまだ含まない。

| Depth | Checkpoint込みROM transaction | Outer max host/client | 反復 | 判定 |
| ---: | ---: | ---: | ---: | --- |
| 1 | `3.537/3.478ms` | `22.382/22.384ms` | 1 run | `25/33ms`超ゼロ |
| 2 | `6.340-6.547ms` | 全2 run最大`22.816ms` | 2 runs | 全roleで`25/33ms`超ゼロ |
| 7 | `9.669-10.321ms` | run 1=`30.966/23.365ms`、run 2=`22.946/24.148ms` | 2 runs | hostに単発`25ms`超、`33ms`超ゼロ |

depth 7のROM transaction自体は4 role sampleで狭い範囲だが、outer hostに一度だけ`30.966ms`が出た。再現runでは消えたためcheckpoint保存の必然的コストとは断定しない一方、最大値優先gateでは外れ値として無視もしない。restore接続後はtransactionを複数回発生させてp95/p99と実present deadlineを測る。

したがって現在の独立した判断は次のとおりとする。

- ROM-loopをmain trackとし、full-frame routeは比較baseline/depth 1-2 fallbackへ下げる。
- 毎frame checkpoint保存を足してもdepth 1-2の性能gateは維持したため、次はSlippi同様にCPU/hardware timelineを戻さずgame memoryだけを復元するcheckpoint境界を実装する。full-machine tinycore restoreをそのままROM-loopへ足す設計にはしない。
- 周辺機能を磨かず、game-memory restore + 履歴loop + 必要なcheckpoint再保存を一つのtransactionへ接続し、固定60fpsで複数回訂正のouter p95/p99/maxを測る。depth 2で`25/33ms`境界を再現的に悪化させる、または意味状態・同一role画像を壊すなら原因を解消するまで先へ進まない。
- そのgateを通った場合だけdepth 4の同一role画像、audio/event side effectを再検証する。旧depth 4画像不一致はCPU回帰修正で自動的に直る種類ではないため、未解決のままとする。

## 2026-08-02 exact depth/presentation gate

### 結論

`ARMv5::JumpTo()` hot-path regression修正後のJIT + `tinycorepreimage` full-machine rollbackは、tested MvL movement routeのdepth 1-2について、状態・描画・固定60fpsの三条件を満たした。この節の「Slippi風ROM loopへ切り替える根拠はない」という比較判断は、その後のpost-hotfix ROM-loop再測定で撤回した。接続済みfallbackはfull-frame routeのままだが、ROM-loopはbounded feasibility trackとして再開する。depth 3-5はfull-frameでは性能測定だけ、depth 4は訂正区間のsprite位相差、depth 6-7はouter `33ms`超過があり、現時点のfull-frame製品安全域はdepth 2までとする。

診断用のforced confirmationは1F固定をやめ、`MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_CONFIRM_DELAY_FRAMES=1..180`で過去の任意frameを確認できるようにした。旧boolean環境変数はdepth 1 aliasとして残す。depth 1-7の各peer 10訂正、計20 sampleはすべて指定frame数を再演算した。

| Depth | total p50 | total p95 | total max | restore p95 | replay p95 | intermediate save p95 | outer max |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | `5.091ms` | `5.485ms` | `5.581ms` | `3.481ms` | `1.611ms` | `0` | `22.112ms` |
| 2 | `7.729ms` | `9.198ms` | `9.713ms` | `5.081ms` | `3.133ms` | `1.323ms` | `26.212ms` |
| 3 | `10.577ms` | `10.895ms` | `11.809ms` | `3.670ms` | `4.510ms` | `2.638ms` | `28.309ms` |
| 4 | `12.963ms` | `14.016ms` | `14.585ms` | `3.540ms` | `6.121ms` | `4.525ms` | `31.029ms` |
| 5 | `15.299ms` | `15.782ms` | `15.811ms` | `3.413ms` | `7.217ms` | `5.103ms` | `32.436ms` |
| 6 | `17.466ms` | `18.012ms` | `18.515ms` | `3.473ms` | `8.535ms` | `6.157ms` | `34.338ms` |
| 7 | `20.238ms` | `22.030ms` | `22.322ms` | `4.247ms` | `10.108ms` | `7.533ms` | `37.788ms` |

このsweepは`tinyFlags=0x241`で、SPUをcheckpointへ含めていない性能境界診断である。depth増加に伴いintermediate checkpoint再保存が線形増加する。過去にintermediate保存を一律省略した経路はdense historical rollbackの意味状態を壊したため、表だけを根拠に削除しない。

### Audio and corrected presentation

現行候補はSPU bitを加えた`tinyFlags=0x245`とする。rollback restoreでSPU内部状態を戻し、再演算中の`SPU::BufferAudio()`は内部音源を通常どおり進めつつ、生成sampleをhost output FIFOへ書かず件数だけ記録する。これにより、再生済みframeのsampleを二重にqueueする問題を避ける。

- depth 1、各peer 9訂正: 計18訂正のtotal p95/max `5.694/5.694ms`、訂正ごとの破棄sample `799-800`、outer max `21.849/21.571ms`、`25/33ms`超ゼロ。
- depth 2、各peer 9訂正: `logs/slippi-audio-depth2-postcorrection-run1`で計18訂正のtotal average/p95/max `7.506/7.835/7.835ms`、破棄sample `1599-1601`。host/clientとも同一seed・同一roleの無訂正controlに対し、訂正後の全trace field差分ゼロ。active `60.02/60.00fps`、outer max `25.100/24.450ms`、`33ms`超ゼロ。
- visual depth 1: frames 900-1200の11枚/role、計22 PNGが同一role controlと完全一致。
- visual + audio depth 2: `logs/slippi-visual-audio-depth2-postcorrection-run1`の計22 PNGがSHA-256までcontrolと完全一致。PNG capture込みで`60.00/60.00fps`、outer max `24.950/24.382ms`。
- visual depth 4: 背景、UI、座標は一致したが、訂正区間中はplayer sprite領域だけ約`340-376 pixels`異なり、後に自然収束した。現時点のcorrected-image gateはfail。
- full VRAM追加の`tinyFlags=0x2C1`はcheckpoint約`922KB`となり、3回目付近でstallしたため棄却した。軽量なGPU 3D subset追加も単発診断では意味状態差を減らしたが、訂正前に採取されるCSVと訂正後に採取される画面の境界差が原因だった。`0x245`の訂正時刻をtrace採取点からずらすと全field一致したため、GPU state追加は採用しない。

SPU state/output処理は「内部状態を正しい時刻へ戻す」「再演算音を重複queueしない」ことまでである。既にspeakerへ出た誤予測音は取り消せない。波形capture比較、実聴、必要な場合の短いcrossfadeは未検証であり、audio gate完了とは扱わない。

### Current blocker and next action

- **昇格済み:** tested movement routeのexact depth 1-2。JIT、software renderer、full-machine state、SPU restore、replay render/audio output suppressionを組み合わせ、状態・画像・60fps gateを通過。
- **未解決:** 実packet jitter/lossで訂正がdepth 1-2へ収まる割合、burst時のpresent-to-present時間、死亡/復帰・土管・item・result/restartのevent duplicate、長時間実行、音声波形と体感。
- **方針:** 接続済みfull-frame product policyは通常depth 1-2までとし、それを超えたら深いcatch-upを常用せず短いstallへ落とす。並行するROM-loop研究の優先事項は上節のcheckpoint込みperformance/presentation gateであり、それを通るまではproduction周辺を磨かない。

## 2026-08-02 ARM9 hot-path regression resolved

### 結論

現行フォークが遅かった主因は特定・修正できた。Commit `f8afb92f557759ed7b5853fd71a9632fb07151cb` が StageScene 診断用に `ARMv5::JumpTo()` へ追加した `NSMLEnvFlag("MELONDS_NSML_BAD_JUMP_TRACE")` が、診断無効時にも全 ARM9 jump で `getenv()` を呼んでいた。PC/LR/SP/CPSR と jump target も毎回不要に取得していた。一般 runtime hook を compile-out した過去の A/B は、この直接呼び出しが gate の外だったため主因を除去できていなかった。

修正は bad-jump flag を process 内で一度だけ読み、無効時には register capture と PU target check を実行しない。既存 runner は process 起動前に flag を設定するため、launch-time config として cache しても診断用途を保てる。flag 有効の 600-frame route smoke も pass した。

実 MvL gameplay の fixed-frame/state-trace 測定は次のように変わった。

| 条件 | 修正前 active FPS / frame | 修正後 active FPS / frame | 改善 |
| --- | ---: | ---: | ---: |
| JIT off | `21.68fps` / `46.117ms` | `264.55fps` / `3.781ms` | `12.20x`、frame time `-91.8%` |
| JIT on | `91.90fps` / `10.882ms` | `530.04fps` / `1.886ms` | `5.77x`、frame time `-82.7%` |

修正後の trace は scene `0x3`、`vsMode=1`、両 player actor 存在を確認し、双方とも `16/25/33ms` 超がゼロだった（`logs/rom-jit-matrix-20260802-badjump-cache-mvl`）。したがって「改造 ROM が本質的に重い」「DS emulation がこの PC で限界」「JIT backend 自体が遅い」という仮説は主因説明として棄却する。

### Rollback feasibility after the fix

前節までの full-frame rollback 不可判定は、replay `RunFrame()` がこの hot-path regression を含む測定だったため撤回する。固定 seed、JIT、host/client を別 logical CPU に固定した exact-one-frame probe を同条件で再実行した結果、各 peer 10 回、計 20 回がすべて `frames=1`、confirmed input replay、1,250-frame cross-peer semantic comparison pass となった（`logs/slippi-exact-one-frame-probe-badjump-cache-run2`）。

| 指標 | p50 | p95 | max | `16.667ms` 超 |
| --- | ---: | ---: | ---: | ---: |
| restore + 1F replay total | `5.091ms` | `5.485ms` | `5.581ms` | `0/20` |
| restore | `3.318ms` | `3.481ms` | `3.886ms` | `0/20` |
| replay `RunFrame()` | `1.484ms` | `1.611ms` | `1.714ms` | `0/20` |

この固定 60fps run 全体も host/client `59.90/59.95fps`、active frame max `22.081/22.112ms`、`25/33ms` 超ゼロだった。訂正処理の外に通常 current frame があるという以前の測定境界の注意は今も正しいが、今回は outer active-frame timer でも短い上限に収まっている。少なくとも local exact 1F について「通常 frame を足すと必ず 60fps 不可能」という旧結論は成立しない。

描画あり software-renderer の standard regression は rollback 無効の条件で 3,000 frames を pass し、active FPS `59.99/59.98`、`33ms` 超ゼロ、両 peer 10 枚ずつの screenshot に blank/corrupt frame はなかった（`logs/badjump-cache-standard`）。これは通常描画の回帰がない証拠であり、rollback 訂正直後の visual exactness、audio duplicate、2F 以上の catch-up を証明するものではない。

実現可能性は現時点で次のように更新する。

| 目標 | 現在の判断 | 根拠／未検証点 |
| --- | --- | --- |
| 通常 MvL を安定 60fps | **高い・local gate pass** | 描画あり 3,000F、約60fps、33ms超ゼロ |
| exact 1F correction を大きな hitch なしで処理 | **高くなった・headless local gate pass** | correction p95 `5.485ms`、outer max `22.112ms`、semantic pass |
| 描画・音声を壊さない exact 1F correction | **未確定** | 通常描画は pass。訂正 frame の screenshot/audio gate は未実行 |
| exact 2-7F rollback window | **未確定** | 旧 full-frame/ROM-loop timing は regression 汚染。修正後の深度別実測が必要 |
| WAN で継続的に快適 | **未確定** | packet jitter/loss、burst correction、長時間 event route を未再測定 |

JIT off も `264.55fps` まで回復したため correctness 比較には使えるが、JIT on は `530.04fps` でさらに約2倍速い。現時点の production 候補は JIT 有効の full-machine `tinycorepreimage` exact rollback とし、no-JIT は比較・fallback 候補に留める。Slippi 型 ROM loop を捨てたわけではないが、旧 timing は同じ regression を含むため、その production 却下も確定判断ではなくなった。ただしまず、より単純で semantic gate 済みの full-frame route を再評価する。

### Follow-up

exact depth 2-7、corrected screenshot、SPU state/output suppressionは上の現行節まで完了した。実WAN相当分布とevent/audio wave gateは未完であり、以後は上節のblockerとnext actionを正とする。

## 2026-08-02 external rollback implementations and full-history audit (historical, superseded above)

> この節の「低い／停止」判断と timing 値は、後に特定した `ARMv5::JumpTo()` の hot-path regression を含む。外部実装の source 調査と state-boundary の説明は有効だが、現行フォークの性能判定と次 action は上節が優先する。

### 結論

ロールバックという方式自体や、Slippi から得た着想を捨てたわけではない。今回中止するのは、現在の melonDS JIT と NSMB ROM 内ループのまま、快適な exact rollback を小さな最適化の積み重ねで製品化する作業である。

Tango/Slippi 以外の実装、過去の rollback / offline replay / software renderer ブランチ、関連 Codex タスクの原ログを監査したが、現行構成で「訂正が発生しても描画を乱さず、表示を止めずに 60 fps」を満たせる根拠は得られなかった。外部実装にも、重いエミュレータの再実行を不要にする一般解はなかった。

実現可能性は次のように分けて判断する。

| 目標 | 現在の判断 | 根拠 |
| --- | --- | --- |
| 現行 melonDS の full-frame exact rollback を訂正時も hitch なしで 60 fps | **低い** | 1F 訂正処理だけで p95 `20.626ms`。その外側に通常フレーム、checkpoint、未計測のコピーが残る |
| Slippi 相当の 2F 以上の快適な rollback window | **非常に低い** | ROM loop depth 2 は `16.720-28.513ms`、full-frame はさらに通常フレームが必要。depth 4 は描画位相も不一致 |
| input delay/pacing で訂正をほぼ防ぎ、稀な 1F 訂正時だけ短い hitch を許す hybrid | **技術的には可能性あり** | exact 1F の選択入力と 1,250F semantic gate は通る。ただし「常時快適な rollback」ではない |
| official 1.1 相当の軽い core へ現機能を移して再評価 | **不明だが優先調査価値あり** | official 1.1 の実 single-player は no-JIT 中央値 `143fps`、JIT 中央値 `297fps`。ただし patched MvL と rollback restore は未測定 |
| 別の DS core または trace/superblock 級 JIT 刷新後の再評価 | **不明** | ARM9 支配は確認済み。DeSmuME だけは別 JIT の実測価値があるが、現時点で同一条件の性能・決定性データはない |

### ROM patch と emulator fork の性能分離

同日の追加測定により、「改造ROMそのものが大幅に重く、JIT必須になった」という仮説は主因としては棄却寄りになった。1-process、同じcurrent fork、同じboot intervalでunmodified ROMとpatched direct-MvL ROMを比較すると、active FPSはJIT offで`31.25`対`30.48`、JIT onで`132.33`対`130.89`だった。patched側の差はそれぞれ`2.46%`、`1.09%`に留まる。詳細な条件と再現scriptは`docs/nsmb-mario-vs-luigi-online-poc.md`の2026-08-02節を参照する。

一方、patched ROMで実際のMvL gameplayへ入ったframes 900-1499はJIT off `22.57fps`、JIT on `92.89fps`だった。state traceでscene `0x3`と両player actorを確認済みである。これはgameplay routeがcommon bootより重い証拠だが、unmodified ROMには同じ二人対戦sceneへのcontrol routeがないため、「native MvL scene」と「追加patch code」の内訳は未確定である。

official melonDS 1.1のunmodified ROMを通常GUIから実single-player gameplayまで自動遷移させると、software renderer／frame limiter offでJIT offは最後の9 sampleが`134-152fps`、中央値`143fps`、JIT onは`273-308fps`、中央値`297fps`だった。画面captureでも実gameplayを確認した。一方、current forkを同じGUI入力・software renderer・JIT offで走らせると、同じ実gameplay画面が`19fps`だった。

前段階では、current `src/ARM.cpp`のtag `1.1`比`+3293/-9`行とper-instruction runtime hookから、fork固有のdiagnostic branchを主因と推定した。しかし新しいcompile-time A/Bがこの判断を覆した。ARM runtime/trace gateとNDS watch-write gateを定数falseへ構造的に落としても、actual MvLはcompute rendererでJIT off `21.68fps` / JIT on `91.90fps`、software rendererで`21.03fps` / `87.22fps`だった。phase breakdownはno-JITの`NDS::RunFrame()`が`44.632ms`、before/after hook合計が約`0.010ms`である。rendererを揃えたunmodified scene `0x4`も`15.35fps`のままであり、診断branch、frontend hook、renderer、改造ROMだけでは公式版との大差を説明できない。

この証拠により、「DS emulation自体が常にぎりぎり」という表現は撤回する。ただしno-JIT rollbackを速度解として採用する結論は導けない。公式版でもJITはinterpreterの約2.1倍速い。no-JIT中央値`143fps`から単純換算した通常1F+訂正1Fは約`14.0ms`で、現restore p95 `3.882ms`を足すだけで約`17.9ms`となり、checkpoint/presentation前に60fps予算を超える。しかもこれは軽いsingle-playerで、actual MvLのclean-core値ではない。no-JITの利点はJIT cache復元を避けられる正しさ・実装単純性であり、forward/replay性能ではない。

現時点の優先調査は、tag `1.1`相当coreとcurrent branchの間をvisible single-player no-JIT gateでbisectまたはcomponent transplantし、`NDS::RunFrame()`内の性能退行を特定することに変える。回復候補が見つかった場合だけpatched MvLのfixed-frame/state-trace matrixへ上げ、strict forward-onlyで最低`150fps`へ届くかを判定する。届かなければno-JIT exact rollbackは実装しない。JITを含むclean core移植とDeSmuME phase-zero benchmarkは、その結果と移植費用を比較して次順位を決める。

### 代替 DS emulator core の監査

「DS だから遅い」のではなく、CPU 実行器、memory fast path、event scheduler、accuracy 方針、state 境界の違いで別 emulator が速くなることはあり得る。ただし、通常プレイの最高速度と exact rollback 適性は別である。2026-08-02 時点の各公式 source を読み、次のように判断した。

| Core | CPU 実行方式 | State/rollback 面 | 現在の判断 |
| --- | --- | --- | --- |
| [DeSmuME JIT](https://github.com/TASEmulators/desmume/blob/master/desmume/src/arm_jit.cpp#L4245-L4307) | x86/x64 basic-block JIT を実装済み | [標準 full-state load](https://github.com/TASEmulators/desmume/blob/master/desmume/src/saves.cpp#L1348-L1418) は `NDS_Reset()` を呼び、そこで [JIT cache 全体を reset](https://github.com/TASEmulators/desmume/blob/master/desmume/src/NDSSystem.cpp#L2688-L2758) する | **唯一の優先 benchmark 候補**。forward-only strict gameplay が melonDS を大幅に超える可能性は未測定。rollback には reset/cache purge を避ける専用 in-memory restore が別途必要 |
| [Dust JIT stub](https://github.com/kelpsyberry/dust/blob/main/core/src/cpu/jit.rs) | desktop frontend は interpreter。`jit.rs` は `TODO` のみ | [`Savestate` derive](https://github.com/kelpsyberry/dust/blob/main/core/src/emu.rs#L88-L154) で CPU、scheduler、RAM、GPU、audio、Wi-Fi などを包括し、load 後の mapping 再構築も持つ | State 設計は参考になるが、現行実行器に melonDS JIT を超える根拠がない。第二優先以下 |
| [NooDS interpreter](https://github.com/Hydr8gon/NooDS/blob/master/src/core/arm/interpreter.cpp#L143-L155) | ARM9/ARM7 を opcode 単位で交互実行する interpreter | [全 component](https://github.com/Hydr8gon/NooDS/blob/master/src/core/save_states.cpp#L82-L155) を順次 `FILE*` へ save/load。完全性は狙っているが rollback 用 preallocated memory path ではない | ARM9 支配の NSMB で速度優位を期待する構造的根拠が弱い |
| [SkyEmu execution loop](https://github.com/skylersaleh/SkyEmu/blob/dev/src/nds.h#L6966-L7046) | ARM9/ARM7 の instruction interpreter と cycle/event fast-forward | [libretro state](https://github.com/skylersaleh/SkyEmu/blob/dev/src/libretro.c#L760-L774) は `nds_t` 全体の `memcpy` と pointer 再構築で、snapshot 自体は rollback 向き | State は最も軽そうだが、full-frame の CPU 再実行が支配する。NDS core の互換性・決定性・映像 gate も未検証なので、速度実測なしには採用できない |

したがって、代替 core の探索は否定しないが、全面移植から始めない。current forkのJIT-enabled gameplay profileで明確なshared overheadを除去できない場合、代替coreとして最も安いphase-zeroは、同じPC、同じpatched NSMB、同じgameplay route、software/native-resolution、audio/presentation無し、state操作無しでDeSmuME JITのstrict forward-only速度を測ることである。安定して最低`120fps`、できればrestore/host余裕を持つ`150fps`以上へ届かなければ、その時点で中止する。平均値だけでなくp95 frame timeとgameplay/state hashを取る。

この gate を通った場合だけ、JIT cache を保持した complete in-memory state の save/restore、同一入力の反復 hash、rollback 後 screenshot、訂正を含む present-to-present interval の順に検証する。stock savestate のように load ごとに JIT cache を消す経路は採用しない。その後にも、現在 melonDS 固有の ROM hooks、packet bridge、local-player/Wi-Fi、desktop integration を移す大きな工数が残る。閉源の DraStic や NO$GBA は「別実装が高速になり得る」参考にはなるが、core 改造と完全 state 検証ができないため採用候補にしない。

この phase-zero benchmark は、現行 melonDS rollback の周辺を磨く作業を再開するものではない。また、ベンチをまだ実行していないため「DeSmuME なら解決する」とは判断しない。

### 外部実装で確認できた方式

確認した実装はいずれも、Slippi のようにゲーム固有更新関数だけを進める方式ではない。正しい機械状態を復元し、通常のエミュレーションフレームを現在まで再実行し、再実行中の host 映像・音声・pacing だけを抑止する。内部の CPU、GPU、音源、割り込み、タイマーは進める。

- [GGPO](https://github.com/pond3r/ggpo/blob/master/src/lib/ggpo/sync.cpp) は、最古の誤予測 state を load し、`advance_frame` callback を現在まで繰り返す制御ライブラリである。エミュレーションを速くする機能は持たない。
- [Fightcade FBNeo](https://github.com/fightcadeorg/fightcade-fbneo/blob/c95950140e2424643a0bb589dc444f0defe03155/src/burner/win32/fbn_ggpo.cpp#L264-L378) は全登録 state を毎フレームコピーし、rollback 中も `RunFrame(0, 0, 0)` でアーケード機械全体を進める。古いアーケード core が十分軽いから成立する例である。
- [RetroArch Netplay](https://github.com/libretro/RetroArch/blob/3f2664303bf46c75174b0c5e89e2c6787794b8b4/network/netplay/README#L61-L83) は core 全 state の ring、予測入力、unserialize、`core_run()` replay、stall/fast-forward/input latency を組み合わせる。melonDS の現行 full-frame route と同じ系統である。
- [bsnes-netplay](https://github.com/HeatXD/bsnes-netplay/blob/0715c67e70a3201708518ac5650c27c478f9ab97/bsnes/target-bsnes/program/netplay.cpp#L172-L201) も SNES 全 state を復元して `emulator->run()` を繰り返し、host PPU/DSP 出力だけを止める。軽い機械全体を再実行する例であり、第三者 fork の成熟度には留保がある。
- [RMG-K](https://github.com/Jay-Day/RMG-K/blob/0f249b52b968637424d6910d9ee880f27334a95f/Source/3rdParty/mupen64plus-core/src/main/main.c#L1103-L1185) は N64 の CPU と機械イベントを次の VI まで実行し、Video/Audio/Pacing/Input の host 出力を無効にする。約 8.4MiB の state を事前確保 buffer に直接保存する新しい実装であり、「複雑な機械でも十分な core 速度があれば full-frame rollback は可能」という証拠にはなるが、長期成熟の証拠にはしない。

最も移植価値があるのは [Flycast Dojo](https://github.com/blueminder/flycast-dojo/blob/d0e47e572b1e7b355e88bda8308c89d0c5156cbf/core/network/ggpo.cpp#L255-L313) である。Dreamcast/Naomi 全体を `emu.run()` で 1F 再実行する点は同じだが、RAM/VRAM/AICA RAM は 4KiB page の write fault で最初の書き込み前 image だけを保存し、rollback 時に dirty-page preimage を逆順適用する。[write-protection/COW 実装](https://github.com/blueminder/flycast-dojo/blob/d0e47e572b1e7b355e88bda8308c89d0c5156cbf/core/hw/mem/mem_watch.h#L32-L94) と、VRAM 復元前に [threaded renderer の safe point を待つ処理](https://github.com/blueminder/flycast-dojo/blob/d0e47e572b1e7b355e88bda8308c89d0c5156cbf/core/hw/pvr/Renderer_if.cpp#L490-L508) は参考になる。

melonDS の `tinycorepreimage` は、各 checkpoint で 4MiB Main RAM 全域を前 frame shadow と `memcmp` し、最後に shadow 全体を再コピーする。restore では最新 shadow 4MiB をまず全コピーし、preimage page を逆適用した後、Main RAM 全域について JIT invalidation を走らせる。Flycast 型 dirty-page tracking ならこの save/restore 部分を減らせる可能性がある。しかし current exact-1F の p95 は restore `3.882ms` に対して replay `RunFrame()` `16.401ms` であり、restore をゼロにしても replay 単体で表示予算をほぼ使い切る。したがって、この移植だけでは製品判定は変わらない。

### 過去ブランチ・Codex タスクから確定したこと

Git 履歴と 2026-05-25 以降の関連 Codex session 原ログを監査した。初期 full savestate、`corelite`/`coredelta`/`tinycore`、Plan-D、shadow NDS、authority sync、offline replay、software renderer、今回の Slippi 型まで、成功報告が後の強い測定で覆った箇所も含めて確認した。

- 初期 full savestate は再収束する経路があったが、19MiB 級 restore と複数 `RunFrame()` で手動停止感が出た。RAM だけへ縮めると CPU/timer/scheduler 不足で分岐した。
- `corelite`/`coredelta` は state が大きく restore が重い。`tinycore` は約 269KiB まで縮んだが、長時間・実 rollback で低 FPS または state 不足が出た。「軽くすると不完全、完全にすると重い」という境界は変わっていない。
- Plan-D と AuthorityOwnerEvents は短い route では軽かったが、死亡・復帰・hazard・spawn/item・remote actor で永続不一致を起こした。後者は正式な world replication へ設計し直せば別製品方式になり得るが、exact rollback ではない。
- `codex/software-renderer-60fps` の controlled 2-process 測定では software GPU 全体が約 `1.1ms/frame`、通常 `RunFrame()` が約 `13.5-14.6ms`。描画 backend は主因ではない。
- `codex/offline-replay-3x` は network、restore、Qt presentation のない有利な条件でも、正しい gameplay の再現値が約 `80fps`、過去最良約 `88.7fps` だった。ARM9/JIT が `86.54%` を占め、180fps は未達。広い render/GXFIFO/scheduler skip は効果なし、回帰、または早期 state mismatch だった。
- 今回の ROM loop は selected 172 fields と depth-1 endpoint image を一致させたが、depth 1 kernel `12.624-14.470ms` は通常 full-frame replay `9.145-12.249ms` より遅い。depth 2 は budget 不合格、depth 4 は endpoint `762` pixel、通常 recovery 後も `322` pixel の差を残した。LCD/OAM/VBlank 位相を消せていない。
- exact full-frame 1F probe は全 20 回 `frames=1`、confirmed input replay、1,250F semantic comparison を通した。しかし affinity-isolated p50/p95/max は `17.442/20.626/21.543ms` で、20 件中 15 件が `16.667ms` を超えた。

### 60 fps 判定の訂正

これまでの top-level note は「1F correction total が `16.667ms` 未満」を promotion gate としていた。この基準は快適性を過大評価するため撤回する。

コード上、`ResimulateIfNeeded()` は before-frame hook 内で過去 frame を `RunFrame()` して current state まで戻した後に return する。その後、同じ通常経路が current frame の checkpoint を保存し、さらに通常の current `RunFrame()` と presentation を行う。また、記録される `rollbackTotalUs` は `BuildPreimageRestore()` 内の state/vector copy、rollback 後の 4MiB shadow refresh、通常 checkpoint save、通常 current frame を含まない。

したがって `20.626ms` は「訂正を含む表示 interval」ではなく、その中に追加される correction 部分だけの楽観値である。仮に exact-chain/self-loop や Flycast 型 COW で correction を `16ms` 未満へ下げても、通常 frame 約 `13.5-14.6ms` と host/checkpoint 負荷が別に続くため、hitch なし 60 fps にはならない。通常 frame の残り時間は数 ms 以下であり、offline replay 最良の 1F 約 `11.3ms` ですらそこへ入らない。

これは「平均 60fps」や「訂正処理が 1 display interval 未満」と、「訂正が起きた表示 frame も止まらない」を分ける証拠である。今後、後者を主張する場合は present-to-present の corrected interval 全体を測る必要がある。

### 現行方針と停止条件

性能優先という要件に従い、次の実装は進めない。

- current full-frame probe への小規模 JIT tweak の追加
- Flycast 型 dirty-page COW の製品実装
- ROM history table の production serialization
- Plan-D の field repair 追加、guest render/collision の個別 skip

いずれも独立には `RunFrame()` 支配を解消せず、60 fps を満たさない状態の周辺を磨く作業になるためである。既存 branch は研究証拠として保持し、rollback は既定無効のままにする。

再開するなら、次のいずれかを別規模の研究として明示的に選ぶ。

1. tag `1.1`相当coreとcurrent branchの性能差を、visible single-player no-JIT gateでbisectまたはcomponent transplantする。回復した最小候補だけをpatched MvL fixed-frame gateへ上げ、JIT off/onの双方を測る。
2. clean-core patched MvLがstrict forward-only `150fps`へ届いた場合だけ、no-JITのcomplete in-memory restoreとpresent-to-present exact-1F訂正を試す。届かなければno-JIT案はそこで停止する。
3. melonDS内で届かなければDeSmuME JITのphase-zero forward-only benchmarkだけを行い、上記`120fps` / p95 / hash gateで即時判定する。通った場合だけrollback stateとmelonDS固有機能の移植可能性を調べる。
4. trace/superblockなどmelonDSのARM9 JIT backendを刷新し、まずrollback無しのstrict offline gameplayで最低`120fps`、実用上はrestore/host余裕を含めてそれ以上を安定達成する。その時点でfull-frame exact rollbackを再評価する。
5. Slippiに近い、DS hardware timelineから独立したさらに小さいNSMB game-state update boundaryを新たに特定する。最初のmilestoneは1 tickを通常frameの空き時間へ収めることと、死亡・復帰・土管・hazard・item・audioを含むsemantic/visual recovery gateであり、現ROM loopの延長をproduction化することではない。

いずれも成功保証はなく、現時点では推奨する短期ロードマップではない。短期の製品方針は input delay/pacing を主とする。稀な訂正時の 1F hitch を許容する要件へ変更するなら、exact full-frame route を hybrid fallback として再検討できるが、それを Slippi 相当の快適な rollback とは呼ばない。

## 2026-08-01 ROM-side renderless game-loop PoC

### Historical judgment (superseded by the 2026-08-02 audit)

The ROM-side loop is semantically more promising than the direct `ProcessList::execute()` experiment, but it is no longer the recommended production path. A diagnostic ROM patch loops through NSMB's own main game path, advances `Math::frameCounter` naturally, and skips the render process list plus `Font::updateFont()` on intermediate catch-up ticks. A guest-owned seven-entry input table drives the loop without an interpreter instruction hook, including under JIT. The tested transactions matched all 172 selected gameplay fields, and the depth-1 endpoint image was exact. Nevertheless, the depth-1 ROM kernel alone takes `12.624-14.470ms`, while the existing full-frame resimulation measured `9.145-12.249ms`; adding the required restore makes the ROM path slower before it gains any multi-frame capability. Depth 2 and above already fail the host-frame budget.

This is not an exact rollback implementation or a playability claim. Per the performance-first gate, production input-table serialization and network-facing activation are deliberately paused. The network-facing rollback runtime and GUI defaults remain unchanged.

**Blocker found at this stage:** the existing full-frame path has a correctness-preserving exact-one-frame probe, and even its correction-only timing fails `16.667ms`. Twenty corrections under `logs/slippi-exact-one-frame-probe-samples-run2` had total p50/p95/max of `17.732/20.160/21.044ms`; 17 of 20 exceeded budget. Pinning the two peers to separate logical CPUs under `logs/slippi-exact-one-frame-probe-affinity-run1` did not rescue it: p50/p95/max were `17.442/20.626/21.543ms`, with 15 of 20 over budget. The affinity-isolated p95 components were `3.882ms` restore and `16.401ms` `RunFrame()`. The later audit above found that this timer also excludes the normal current frame and other outer work, so it was not a sufficient stable-60-FPS gate.

**Disposition after the 2026-08-02 audit:** the planned exact-block-chain/self-loop micro-benchmark is cancelled as a production gate. Even a correction-only result below `16.667ms` would leave the following normal frame outside the measurement. Keep the ROM loop as research evidence and the existing full-frame route only as a possible hitch-tolerant fallback. Do not serialize the guest history table into production.

### Performance and corrected-presentation gate

The frame-boundary probe now records total wall time, maximum `NDS::RunFrame()` wall time, and the number of emulated display frames involved in each history transaction. It supports a symmetric mode where both peers run the renderless catch-up, avoiding the artificial case where one corrected peer waits on a normally advancing peer. The input sequence and match seed are fixed for cross-run image checks, and screenshot runs use the software renderer because the headless compute renderer exposes no framebuffer to the test hook.

The offset-2 symmetric JIT measurements were:

| Catch-up depth | Host total | Client total | Display frames | Result |
| ---: | ---: | ---: | ---: | --- |
| 1 | `12.452-18.159ms` across four repetitions | `12.118-16.546ms` across four repetitions | 1 | unstable; one role sample exceeded `16.667ms` |
| 2 | `25.569ms` | `19.329ms` | 2 | fail |
| 3 | `23.292ms` | `31.966ms` | 2 | fail |
| 4 | `23.978ms` | `26.262ms` | 2 | fail |
| 7 | `39.657ms` | `34.341ms` | 2 | fail; each peer also had a single-frame segment above `21ms` |

Offset 1 is the earliest viable guest-history start; offset 0 misses the exact counter gate and never starts. Offset 1 did not fix the gate: depth 1 through 4 all crossed two display frames, and symmetric depth 4 was `25.981/25.852ms`. The earlier offset-2 measurements are therefore not rejected as mere reservation-wait overhead; alignment changes the result, but neither tested viable alignment provides a robust one-frame correction window.

The corrected image gate used identical match seed `0x12345678`, history base tick `0x0051`, and input hash `7C3924A2`. Comparing the host role against a separately run normal-control host found `281/98,304` changed pixels (`0.285848%`, bounding box limited to the two character rows) at the completed depth-4 endpoint. After one normal recovery tick, `292/98,304` pixels (`0.297038%`) still differed and the bounds also reached a small lower-screen region. There is no blank frame or large background corruption, but the player sprite/presentation phase does not exactly converge, so the visual gate is not passed.

An attempted optimization kept `Font::updateFont()` suppressed on the final catch-up tick while restoring only the process-list render. It preserved the 172-field semantic result but worsened the symmetric depth-4 measurement to `37.273/34.389ms` and carried extra stale-UI risk. That variant was rejected and reverted; the retained patch fully renders the final catch-up tick.

The retained performance evidence is under `logs/slippi-perf-symmetric-depth*-run*` and `logs/slippi-perf-immediate-symmetric-depth*`; the fixed-condition image evidence is under `logs/slippi-visual-fixedtick-depth4-host-{target,control}*`.

### JIT-safe stage profile

The diagnostic ROM now writes six stage markers through an otherwise-unused NO$GBA debug-register slot: tick begin, input begin/end, render begin/end, and tick end. melonDS timestamps these writes with a host monotonic clock and ARM-system clock under JIT; frame-begin/end markers make VBlank crossings explicit. Stage tracing is opt-in through the frame-boundary harness and ordinary stable ROMs never contain these writes.

In `logs/slippi-stage-profile-depth1-run3`, the symmetric depth-1 transaction used `136,607/137,120` emulated cycles and touched one display frame. Only `138,799/138,790` cycles remained from transaction start to that frame's end, leaving about `1.7-2.2k` cycles of emulated margin. Host/client input took `103/129us`, pre-render game update `4,315/4,570us`, final guest render-process execution `7,068/7,799us`, and the post-render tail `174/216us`. The large render segment is ARM9-side render-list work, not the previously measured `~1.1ms` software GPU rasterizer alone.

In `logs/slippi-stage-profile-depth4-run2`, both roles deterministically needed two display frames. Completion consumed `265,360/265,873` emulated cycles versus the same `~138.8k` first-frame remainder. Input was only `285/242us`; four pre-render updates totaled `14,988/13,993us` and `168,316` cycles; the render segments totaled `6,549/7,923us`, of which the final render alone was `6,540/7,917us` and about `92k` cycles. Intermediate skipped-render markers consumed only tens of cycles. Host wall time varied with two-process contention, but the nearly identical emulated-cycle result proves that the display-frame spill is architectural rather than a slow-PC artifact. The logger records rows only while the history transaction/recovery gate is active, avoiding pre-probe CSV contention.

The no-stage-log depth-4 control at `logs/slippi-stage-profile-depth4-notrace-run1` still consumed two display frames, so CSV formatting is not the reason for the crossing. The asymmetric regression at `logs/slippi-stage-profile-depth4-semantic` consumed all four historical inputs and matched all `172` gameplay fields both at the endpoint and after recovery. Stage marker register use therefore passes the current semantic gate; it remains diagnostic and is not a production mechanism.

The follow-up process-list trampoline at `logs/slippi-process-stage-depth4-run1` split the `168,759` pre-render cycles without changing native list order. Across four ticks, delete-list execution used only `184` cycles and create-list execution `172`; the gameplay update list used `156,527` cycles (`92.75%` of the measured pre-render total). Host/client gameplay-list wall time was `13,187/12,757us`; delete/create were only `14/13us` and `13/12us`. The remaining roughly `12k` cycles cover input-to-manager helpers, list transitions, and post-list work. The asymmetric `logs/slippi-process-stage-depth4-semantic` regression again matched all `172` fields at the endpoint and after recovery. Removing delete/create lists would therefore add correctness risk for essentially no performance return; the next useful boundary is inside the gameplay update list.

`logs/slippi-jit-block-profile-depth4-run1` enabled an x64 JIT diagnostic counter only for the four gameplay-list calls. Host and client produced the same dominant blocks. Among the retained top 256 blocks, `CollisionMgr::updateBottomSensor(bool)` accounted for `105,880` dynamic guest instructions (`48.7%`) and the ARM `_ll_div` helper for `22,570` (`10.4%`). Other collision-side/top routines, `Base::processUpdate()`, actor pre/post update, player update, input, tile behavior, and process traversal made up most of the named remainder. Candidate render-adjacent entries such as coin animation, particle cleanup, and projection setup were individually around `1%` or less. This profile does not prove that every named instruction is semantically indispensable, but it rules out a safe broad render-cache omission capable of the nearly `2x` emulated-cycle reduction required by the current VBlank phase. The later bounded `_ll_div` replacement did not produce a repeatable host-time gain, and the collision routine could not be bypassed without aborting or stalling gameplay. The profiled asymmetric regression at `logs/slippi-jit-block-profile-depth4-semantic` still matched all `172` gameplay fields at the endpoint and after recovery; the counters are host-only and diagnostic.

The first isolated JIT rebase retained only the strict x64 ARM9 block-chain implementation from `61802285`, excluding its obsolete offline-replay docs/scripts and memory profiler. Because the ordinary runtime-hook safety gate intentionally disables chaining for diagnostic environments, the ROM-probe harness has a separate explicit override; both switches default off. Across `logs/slippi-exact-chain-depth4-{baseline,enabled}-run{1,2}`, the four role samples averaged `26.327ms` baseline versus `25.117ms` enabled, a `4.6%` reduction. The maximum involved-frame average improved from `13.673ms` to `13.196ms` (`3.5%`). Both configurations still consumed two display frames. `logs/slippi-exact-chain-depth4-semantic` consumed all inputs and matched all `172` gameplay fields at the endpoint and after recovery. This is a retained incremental host-time optimization, not a playability-gate pass.

The next `479ac5c6` rebase was rejected after isolating its parts. With its unconditional condition-code lowering and ninth x64 allocation register, the disabled-stack baseline at `logs/slippi-stack-fast-depth4-baseline-run1` averaged `29.231ms`, about `11%` slower than the preceding four-sample baseline. Enabling its stack path recovered to `26.702ms` but remained slower than the retained exact-chain result; combining it with exact chaining was `28.477ms`. After reverting the unconditional condition/register changes and retaining only the opt-in stack fast path, two paired runs gave a median `26.347ms` baseline versus `26.088ms` enabled, only about `1%`, with a large baseline outlier and no display-frame reduction. The stack-only implementation and harness switch were therefore removed rather than retaining complexity on a non-reproducible gain. The raw evidence is under `logs/slippi-stack-{fast,only}-depth4-*` and `logs/slippi-jit-combined*-depth4-*`.

The independent self-loop part of `51381858` was retained without its register-cycle dependency. It recognizes an ARM9 conditional backedge to the current block and re-enters that generated block directly while preserving the exact scheduler-target check. Across `logs/slippi-self-loop-depth4-{baseline,enabled}-run{1,2,3}`, the six role samples reduced total-time median from `28.892ms` to `26.594ms` (`8.0%`) and maximum-frame median from `16.055ms` to `14.379ms` (`10.4%`). Both versions still used two display frames. `logs/slippi-self-loop-depth4-semantic` matched all `172` gameplay fields at the endpoint and after recovery. The switch defaults off and is explicitly enabled only with exact block chaining in the ROM probe.

The same commit's ARM9 cycle accumulator was then isolated with a per-compiler reserved-register mask, so disabling the experiment left the ordinary register allocation unchanged. Across `logs/slippi-register-cycles-depth4-{baseline,enabled}-run{1,2}`, its four-role median worsened from `25.902ms` to `26.098ms`; maximum-frame median worsened from `13.799ms` to `13.937ms`. It did not change the two-display-frame result, so the whole register-cycle implementation and probe switch were removed.

The first hardware-deferral attempt stopped all scheduler events during catch-up. Once its start marker was corrected, ARM9 stalled in a hardware-dependent polling path until the artificial two-million-cycle target, and depth 4 worsened to four display frames in `logs/slippi-freeze-hardware-depth4-stage-run2`. This rules out a blanket Slippi-style hardware freeze for this NSMB loop: at least short-latency events such as division/square-root completion and normal ARM7/peripheral progress must remain available.

The replacement diagnostic defers only `Event_LCD`, leaving ARM9, ARM7, timers, division/square-root, audio, and other scheduled work active. At the final ROM marker it shifts the pending LCD timestamp by the deferred system-clock interval and resumes normal scanline processing. `logs/slippi-defer-lcd-depth4-semantic-run1` completed depth 4 in one display frame for the first time, matched all `172` fields at the endpoint and after recovery, and reduced the curated RAM change-mask mismatch from the preceding `146` bytes to `13` bytes. The no-stage-log symmetric run was `24.597/23.463ms`, still far above `16.667ms`. Fixed-seed same-role software-renderer images under `logs/slippi-defer-lcd-visual-depth4-host-{target,control}*` differed by `762/98,304` pixels at the endpoint (`0.775%`, bounding box `90,115..214,252`) and `322/98,304` after one normal recovery tick (`0.328%`, bounding box `90,115..184,239`). That is worse than the prior `281/292`-pixel result, so LCD deferral passes the display-frame-count boundary but fails the corrected-image gate.

Four symmetric depth-1 repetitions under `logs/slippi-defer-lcd-depth1-performance-run{1,2,3,4}` all used one display frame. Seven of eight role samples were `12.604-13.692ms`, but one client sample was `22.605ms`. The uncontrolled two-emulator result cannot support a stable-60-FPS claim by itself. LCD deferral remains an opt-in diagnostic because the image and depth-4 wall-time gates still fail.

The outlier is not accompanied by evidence of a slower rollback code path. In that same run, the client's immediately following normal recovery tick took `20.264ms`, and the host's recovery tick took `29.143ms` even though its rollback transaction was `12.616ms`. Across the four unpinned runs, normal recovery ticks ranged from `18.794ms` to `29.143ms`. This points to local two-emulator scheduling contention rather than a deterministic depth-1 workload spike, but it does not by itself prove production p95 latency.

The harness can now assign the two local melonDS processes separate processor-affinity masks. Three diagnostic repetitions under `logs/slippi-defer-lcd-depth1-affinity-run{1,2,3}` pinned the host and client to distinct logical CPUs. All six rollback samples were `12.624-14.470ms`, below `16.667ms`; the unpinned `22.605ms` spike did not recur. This supports a narrower conclusion: depth 1 has enough intrinsic compute margin on the tested Ryzen 7 5700X when local benchmark contention is isolated. Affinity is a measurement control, not a proposed production requirement, and depth 4 still fails independently.

The depth-1 visual gate is now stronger than the earlier cross-peer screenshot diagnostic. `logs/slippi-defer-lcd-visual-depth1-host-{target-run2,control}` ran the same fixed-seed host role twice, once as the corrected target and once as the normal control, and captured the software-renderer endpoint at display frame 953. The two `256x384` images matched exactly: `0/98,304` changed pixels. Both runs also consumed the expected input, matched all 172 semantic fields at the endpoint and after recovery, and had only five bytes of curated change-mask mismatch within their respective cross-peer comparisons. Screenshot/software-renderer overhead produced target times of `14.535ms` for the host-target run and `16.830ms` for the client-target run, so these visual runs do not replace the isolated no-draw timing result; they establish presentation equality for depth 1.

Two further core-group-isolated depth-2 repetitions under `logs/slippi-window-depth2-affinity-run{1,2}` measured `16.720/20.347ms` and `17.862/28.513ms`. All four role samples missed the strict `16.667ms` gate or exceeded it materially. Since depth 2 already lacks stable headroom, depth 3-7 are no longer useful monolithic optimization targets under the performance-first requirement. Depth 1 also cannot be used repeatedly to amortize a backlog while the presentation timeline advances: its target and control both finish at display frame 953, so it gains no timeline frame. The later production comparison also found its kernel slower than an ordinary single `RunFrame()`, eliminating the isolated depth-1 ROM fast path.

Measured JIT hot-path experiments did not change that boundary. A diagnostic replacement of the runtime `_ll_div` at `0x0207DF88` with host signed 64-bit division matched all 172 semantic fields, but shortened emulated time by about four thousand cycles, increased the curated change-mask mismatch from 13 to 17 bytes, and had no repeatable wall-time gain: the core-group-isolated four-role median was about `25.47ms` baseline versus `26.48ms` replaced. Expanding the JIT block limit from 32 to 64 instructions regressed both paired roles from `21.926/23.599ms` to `23.350/25.447ms` (about 6-8%). Both variants were removed.

The collision profile was also checked as an intentionally invalid upper-bound experiment. The earlier hot address `0x01FFFDB4` is an inner block, while the symbol file identifies `CollisionMgr::updateBottomSensor(bool)` entry at `0x01FFF93C`. Skipping the correct function globally caused an ARM9 data abort before the probe; restricting the skip to the guest history-active flag avoided startup failure but did not complete within 120 seconds. Thus collision cannot be assigned a meaningful zero-cost timing bound by forging a return value: it is a gameplay-critical state transition, not a replaceable render calculation. The dangerous diagnostic was removed, and no collision HLE is retained.

The production comparison under `logs/slippi-depth1-fullframe-restore-baseline-run{1,3,5}` changes the recommendation. With the current `tinycorepreimage` backend, an ordinary two-frame forced mismatch used `3.3-4.2ms` to restore and `18.6-20.9ms` for two normal `RunFrame()` calls, totaling `24.4-26.7ms` before the later save fix. A timing-only `MaxResimFrames=1` clamp produced one-frame totals of `14.797ms` (`restore=3.644ms`, `run=9.145ms`, final save `1.749ms`) and `17.381ms` (`3.356/12.249/1.479ms`). Even the slow full-frame sample spent less time in `RunFrame()` than the ROM depth-1 kernel's worst `14.470ms`, so connecting the same restore to the ROM loop cannot improve the practical gate.

The audit did find one safe full-frame improvement. `ResimulateIfNeeded()` serialized the final replayed checkpoint, then the same before-frame hook continued to the ordinary `SaveCheckpointIfNeeded()` and serialized that frame again. The replay loop now saves only true intermediate frames when requested and leaves the final frame to the ordinary path. The policy has unit coverage. Timing-only one-frame probes under `logs/slippi-skip-final-resim-checkpoint-run{1,2}` recorded zero replay-loop checkpoint-save time and totals of `16.784ms` and `16.210ms`; ordinary checkpoint counts remained populated. A normal uncapped two-frame mismatch under `logs/slippi-skip-final-resim-checkpoint-semantic-run1` passed the 1,250-frame game-state comparison with zero replay-loop checkpoint-save time. Its `27.138ms` total confirms that the optimization removes redundant work but does not make deep rollback a 60 FPS path.

The follow-up replaced the timing-only clamp with an exact-one-frame diagnostic. When enabled, the prediction probe can deliberately flip an already confirmed real input for one frame, retain that real input, confirm it at the next before-frame boundary, and disable reinjection during replay. Thus every retained measurement restores checkpoint `N`, replays only frame `N`, and uses the original confirmed input; it does not discard an older mismatch. Ten probes per peer under `logs/slippi-exact-one-frame-probe-samples-run2` all reported `frames=1`, zero replay-loop checkpoint saves, and passed the 1,250-frame cross-peer game-state comparison. Their p95 restore, replay, and total times were `4.728/15.144/20.160ms`. Repeating with affinity masks `1/4` produced p95 `3.882/16.401/20.626ms`. This changes the optimization priority: restore is still several milliseconds, but normal frame emulation itself is too close to or over the full display budget on the later gameplay route.

The same work fixed an input-ordering defect exposed by the first probe attempt. `ThrottleFrameLead()` pumps the network, but the runtime previously selected and wrote a predicted remote input before throttling. If the real input arrived during that throttle, confirmation still received the current frame number and was classified as not-yet-simulated even though the stale prediction was already committed to guest scratch. Remote-input selection now occurs after throttling. `logs/slippi-throttle-before-resolve-regression-run1` passed 1,250 frames with delayed/jittered input and a normal prediction probe; its actual late mismatches were classified as already simulated and resimulated, with no `current/future mismatch applied without rollback` event.

Final-render diagnostics under `logs/slippi-final-render-jit-profile-depth4-*` and `logs/slippi-final-render-hwstate-depth4-*` further separate guest rendering from presentation state. For the same host role, the target and normal control execute `66,451` and `66,528` dynamic ARM9 instructions in the final render; only four ARM9 blocks differ, for a net difference of 77 instructions. The larger `0x037Fxxxx-0x0380xxxx` profile differences are ARM7 WRAM code, not ARM9 DTCM or game rendering. At render begin, Main RAM differs at only 15 non-scratch bytes, all in timing/network/SDK state; the render phase then changes the same 182 non-scratch bytes in both paths. By contrast, the endpoint OAM hashes differ while both palette and VRAM hashes match, and the next normal recovery render produces the same OAM hash on both paths. This is direct evidence that the lower-screen error is caused by skipped LCD-driven OAM transfer/presentation phase.

Two bounded alternatives were tested and reverted. Allowing LCD state to advance during catch-up while suppressing intermediate frame presentation preserved one displayed-frame transaction and all 172 semantic fields, but depth 4 was `25.984ms`, the curated change-mask mismatch worsened from 13 to 142 bytes, and the software-renderer visual run failed to complete symmetrically. Releasing LCD deferral at the final render instead of tick end preserved the 13-byte mismatch and semantic result but worsened depth 4 to `28.051ms`. Neither satisfies the performance-first gate, so the retained implementation remains the earlier full LCD deferral through final tick end.

### Why DS emulation has little rollback headroom

An audit of the earlier performance branches and Codex tasks does not support the hypothesis that DS graphics are the main reason for the narrow margin. The software-renderer work from `codex/software-renderer-60fps` is already contained in `main` and this branch. Its controlled two-process measurements put the entire software GPU path at roughly `1.1ms/frame`, while ordinary `NDS::RunFrame()` remained roughly `14-16ms`. Input synchronization hooks were only hundredths of a millisecond. The dominant work is the serial ARM9/ARM7 and peripheral timeline, especially ARM9 execution.

The unmerged `codex/offline-replay-3x` branch tested the more favorable case: one process, no network, no rollback restore, no Qt presentation, and deterministic state gates. Render/presentation suppression produced only about `80fps` in the clean reproducible gameplay measurement. Experimental strict ARM9 block linking, condition/stack fast paths, and resimulation-specific JIT work reached a best historical weighted result of about `88.7fps` (`~1.48x` real time), not the requested `180fps`. The clean profile attributed `86.54%` of gameplay time to ARM9; GPU3D was `1.81%` and ARM7 was `2.47%`. Even removing all measured non-ARM9 work would therefore provide only about `1.16x`. Larger JIT blocks, fixed guest-register holding, ROMCTRL/GXFIFO special cases, native architecture flags, profile-guided optimization, ThinLTO, scheduler changes, and guest render skips were either ineffective, regressed performance, or violated deterministic state gates. These JIT commits may still be worth a separately rebased benchmark because they are not in `main`, but their measured gain cannot make repeated full-frame rollback fit one `16.667ms` presentation interval.

The reason a much faster PC does not automatically provide proportionally faster emulation is that this workload is not the DS's nominal instruction count executed directly on all host cores. A mostly single host thread repeatedly translates short guest basic blocks, maintains ARM condition flags and cycle counts, checks memory/MMIO behavior, returns to the dispatcher, and serially orders ARM9, ARM7, DMA, timers, interrupts, audio, Wi-Fi, and GPU events. Extra desktop cores have limited leverage over that dependency chain. This also explains the earlier battery-mode result where an affected peer's `RunFrame()` increased from the local machine's roughly `14.5ms` to `24.6ms` even though network wait was only `0.13ms`: host single-core frequency and scheduling margin matter much more than aggregate CPU utilization.

This does **not** establish that GameCube hardware is intrinsically easier to emulate than DS hardware, or that a complete Dolphin frame is cheaper than a complete melonDS frame. There is no controlled same-PC Slippi-versus-melonDS full-frame benchmark in the retained evidence. The material Slippi advantage is architectural and title-specific: `SlippiSavestate` copies selected Melee RAM ranges while most full Dolphin CPU/GPU/DSP/audio serialization remains disabled and sound/XFB/VI ranges are excluded; the Melee guest patch then loops its gameplay update function and explicitly triggers rollback “without rendering frames.” Controller reads, interrupts, camera, sound, rumble, music, and stage behavior have dedicated rollback handling. It therefore avoids re-emulating a normal complete GameCube frame for every corrected game tick.

The current NSMB ROM loop follows that direction but has not isolated the game tick to the same degree. Each repeated update still consumes ARM cycles and lets the DS hardware scheduler, ARM7, timers, events, and eventually VBlank advance. The depth-4 display spill and character-region phase error are consequences of that boundary, not evidence that the software rasterizer is too slow. Since depth 2 is the minimum workload that could reduce a growing backlog by one frame per presentation and it already misses budget, an amortized scheduler is not justified. The full-frame comparison also removes the depth-1 case, leaving a trace/superblock or different JIT backend as the general-emulator alternative; the offline experiment shows that this is a substantially larger project than the current rollback integration.

### ROM patch boundary

The diagnostic generator adds `--game-tick-probe` without changing ordinary stable ROM generation. The patch uses a permanent ARM9 code cave, a control/history table, and three gates:

- after the normal frame-counter update, a request counter branches from `0x02004EFC` back to the native loop start at `0x02004EC8`;
- while active, the render-list call at `0x0204D5EC` is skipped;
- while active, `Font::updateFont()` at `0x02004EEC` is skipped;
- input update, scene/game helpers, delete/create/update process lists, priority maintenance, and the native frame-counter update remain on the title's normal path.

The input gate at the native loop start consumes `{packet tick, player 0 keys, player 1 keys}` entries directly from guest RAM, arms the target-side repeat count at a shared game counter, and parks at the gate after the requested count. The park branch rechecks the enable word, so the emulator can release the transaction at a later DS frame boundary. This removes the earlier interpreter-only per-instruction control dependency; it does not yet remove the diagnostic emulator-side table population and snapshot observation.

This is closer to Slippi's game-integrated loop than the earlier emulator-side direct call: the game owns the repeated control flow and all pre-update ordering, while only identified one-shot rendering work is gated.

### Measured result

The counter-aligned one-extra-tick run at `logs/slippi-rom-loop-poc-loop-aligned` produced:

- game counter `729 -> 730` for both the ROM-loop tick and its following normal replay;
- raw extra-versus-normal output difference: `2,559` bytes across `15` pages;
- after excluding only the diagnostic cave and known `0x02288000-0x02289FFF` packet/input ring: `434` bytes across `12` pages;
- known semantic fields remained synchronized through frame 990 apart from expected local-role/camera/state-hash fields and transient CPU sampling fields.

The stronger same-tick renderless A/B and recovery run at `logs/slippi-rom-loop-poc-renderless-recovery` compared a renderless host tick with a normal client tick:

- curated cross-peer difference before the tick: `445` bytes / `17` pages;
- immediately after renderless-versus-normal: `759` bytes / `22` pages;
- after one subsequent normally rendered tick: `404` bytes / `17` pages;
- both peers advanced the game counter from `729` to `730` and the known gameplay trace stayed aligned.

The temporary increase is concentrated in OAM buffers (`0x02087760-0x020887E7`) and actor render/cache pages around `0x021B5000-0x021B7FFF`. Because the cross-peer difference returned below its pre-test baseline after one normal render, these bytes are evidence of disposable/rebuilt render state in this route, not a persistent gameplay divergence. The multi-tick results below support that inference, but event-route validation is still required before those ranges can be excluded from a rollback digest.

The repeated-loop A/B runs extended the same boundary to the planned rollback window:

- `logs/slippi-rom-loop-poc-renderless-ab-2tick`: both peers advanced `729 -> 731`; the target used one display frame while the control used two, and the curated cross-peer difference was `448` bytes / `17` pages before, `819` / `23` immediately after, and `406` / `20` after one normal rendered tick.
- `logs/slippi-rom-loop-poc-renderless-ab-7tick`: both peers advanced `729 -> 736`; the target used one display frame while the control used seven, and the curated difference was `445` / `17` before, `907` / `23` immediately after, and `411` / `20` after recovery.

Thus the native loop can execute the full `1-7` tick diagnostic window without a crash or a persistent increase in the current curated cross-peer difference on the tested movement route. These runs reused the ordinary current input on every repeated tick; they validate loop control and render recovery, not historical-input correctness.

`logs/slippi-rom-loop-poc-historical-input-7tick` then injected a distinct seven-entry input/tick sequence into the existing two-player JIT-helper scratch interface at each ROM-loop start. The renderless target and seven-normal-frame control produced the same sequence hash `1A24E475`, advanced `729 -> 736`, and matched all `172` fields in the new gameplay semantic gate both immediately and after one normal rendered tick. The gate covers scene/game counters, player input and match globals, RNG sample, both player actors' transforms/runtime flags, battle-star transform, and moving-hazard transform. Curated full-RAM differences were `445` bytes / `17` pages before, `1,331` / `26` immediately after, and `445` / `22` after recovery; the remaining page-count increase includes the deliberately modified JIT scratch page and network/render-local state. This is a positive historical-input boundary result, but the sequence is still generated by the diagnostic instruction hook rather than the production input timeline.

`logs/slippi-rom-loop-poc-jit-guest-recovery-7tick` moves that sequence into the guest-owned table and runs with JIT enabled and emulator observation only at DS frame boundaries. Both peers armed from game counter `709`, consumed all seven entries with sequence hash `1A24E475`, and stopped at the same comparison counter `718`; all `172` semantic fields matched. A further deterministic guest-table recovery tick also matched all `172` fields. The renderless target reached the transaction endpoint at display frame `954`, while the normal control reached it at frame `959`, demonstrating catch-up without seven full rendered frames. Curated cross-peer RAM differences remained nonzero (`405` bytes / `18` pages before and `587` / `23` both immediately and after recovery), so this is a semantic-boundary pass, not byte-exact state proof.

The first production integration boundary is now shared rather than duplicated. `InputTimeline::ResolveReplayFrameInputs()` resolves the local input, confirmed-or-predicted remote input, prediction flag, and player 0/1 ordering for one replay frame. The existing full-`RunFrame()` rollback path uses this API, and unit coverage fixes both local-player orderings, confirmed and predicted remote cases, neutral fallback, and invalid player IDs. `logs/slippi-production-input-resolution-rollback` passed the existing 1,250-frame packet-loss rollback checkpoint/golden tier. The API does not yet write the ROM history table, so it proves input selection compatibility rather than guest-loop production activation.

The extra tick advanced ARM9 by about `139k` timestamp units and ARM7 by about `70k`; therefore the ROM loop does not make hardware time disappear. The earlier scheduler-freeze failure remains relevant: hardware handling must be designed rather than globally suppressed.

### Gate status

- Native ROM-loop re-entry: **PASS**.
- One through seven renderless ticks, natural frame counter, known gameplay semantics: **PASS for the tested movement route**.
- Historical RAM render-cache recovery after one normal render: **PASS only for the earlier curated-RAM criterion**. Fixed-seed screenshot convergence is **PASS at depth 1** (`0/98,304` changed pixels) and **FAIL at depth 4**.
- Immediate full-RAM equality: **EXPECTED FAIL** because render/OAM and packet-ring state are not equal.
- Curated gameplay digest: **INCOMPLETE**; current exclusions are diagnostic, not yet promoted.
- Per-tick distinct input via guest-owned history table: **PASS for seven ticks on the tested route**.
- Hook-free guest loop control under JIT: **PASS**; emulator-side diagnostic table population and boundary capture remain.
- Production replay-input selection boundary: **PASS** in unit tests and the existing full-frame rollback regression.
- Guest catch-up wall-time/presentation gate: depth-1 **kernel-only PASS but production REJECTED**; its `12.624-14.470ms` cost is slower than the measured full-frame `RunFrame()` and leaves inadequate restore margin. Depth 2 fails stable timing, and depth 4 fails both timing and image convergence.
- Guest-table serialization from the production boundary and checkpoint restore plus guest catch-up: **REJECTED BY THE PERFORMANCE GATE**.
- Existing full-frame exact-one-frame correction: semantic comparison **PASS**, stable-60-FPS performance **FAIL** (`20.626ms` affinity-isolated p95, 15/20 over budget).
- Event coverage, duplicate audio/network effects, and WAN: **NOT RUN**.

## 2026-08-01 Slippi-style game-tick equivalence PoC

### Historical result of the direct-call attempt

The diagnostic PoC can re-enter the NSMB MvL update process list once and return safely without executing another complete `NDS::RunFrame()`. That is enough to establish that the Slippi direction is technically plausible at the control-flow level. It does **not** pass the exact-equivalence gate: a direct `ProcessList::execute(updateList)` call is not yet equivalent to one normal NSMB game tick.

The network-facing rollback runtime and GUI defaults were not changed. All new behavior is diagnostic-only behind `MELONDS_NSML_GAME_TICK_PROBE`, and the current harness deliberately uses the interpreter because the existing address hook is not a reliable per-instruction boundary under JIT block linking.

**Blocker found in this attempt:** the candidate update call is coupled to state advanced outside the update list and to DS scheduler/hardware side effects. Restoring Main RAM, ARM timestamps, scheduler event records, and the scheduler mask was insufficient to make the following normal tick exact.

**Disposition:** this direct-call boundary remains rejected. The follow-up experiment did move re-entry into the ROM/game-side main loop and produced the improved result documented in the section above; packet/input preservation and the remaining one-shot policies are still pending there.

### Boundary and positive result

ARM9 call tracing established the relevant retail-US MvL boundaries:

- the main loop returns from `ProcessManager::updateProcessLists()` at `0x02004EEC` and later calls `waitVBlankIntr()` at `0x02004F14`;
- `updateProcessLists()` executes delete (`0x0208FB28`), create (`0x0208FB48`), gameplay update (`0x0208FB18`), then render (`0x0208FB38`) process lists;
- `Math::frameCounter` at `0x0208B668` advances by one in the main loop outside the gameplay update-list call. The earlier direct probe's manual step of two reflected its display-frame comparison phase, not the native instruction.

With the counter advanced explicitly, the extra update returned normally. At the update-only boundary, the extra path changed `296` bytes across `13` Main RAM pages, while the peer's normal update changed `310` bytes across the same `13`-page count. In the extra-update-plus-next-frame-skip experiment, known player, actor, and gameplay fields realigned; the frame-930 and frame-960 semantic traces differed only in expected local-role, camera, and state-hash fields. This is evidence that the candidate call performs meaningful gameplay progression rather than merely returning harmlessly.

### Exact-equivalence failure

The close changed-byte counts do not imply state equivalence. The changed-byte masks differed at `520` bytes, because input/packet rings and actor allocations are written at peer- and frame-dependent addresses. A same-instance replay test therefore restored the entire 4 MiB Main RAM after the extra call and compared it with the next normal update from that instance. The outputs still differed at `4,326` bytes across `20` pages. Most of that was in the `0x02288000-0x02289FFF` packet/input ring, but differences also remained in known actor-state pages such as `0x021B5000-0x021B6FFF`.

A stronger isolation attempt prevented ARM9 rescheduling during the extra call, then restored ARM9/ARM7 timestamps, scheduler events, the scheduler mask, and Main RAM. It also failed:

- extra update game counter: `730`; following normal replay: `729`; control normal update: `730`;
- extra versus same-instance normal output: `4,080` differing bytes across `11` pages;
- isolated extra update: `2,229` changed bytes across `19` pages, versus normal update `286` bytes across `12` pages;
- at semantic frame 960 the target was one `netPacketTick` and one moving-hazard step behind the control peer; the divergence remained at frame 990.

This failed variant is important evidence: suppressing scheduler delivery without transactionally restoring all timer, interrupt, DMA, IPC, GPU/SPU, CPU interrupt, and ARM7-visible state changes perturbs the next real tick. Slippi avoids the analogous problem through a game-integrated loop and title-specific side-effect patches; the current melonDS shortcut has not yet reproduced that isolation.

### Implementation and verification

- Added a diagnostic ARM9 hook and RAM/scheduler snapshots in `src/ARM.cpp`, `src/NDS.cpp`, and `src/NDS.h`.
- Added `scripts/run-nsmb-mvl-game-tick-probe.ps1`, which runs the existing split-local packet bridge, captures update/render boundaries, and writes byte/page equivalence metrics to `summary.csv`.
- Passed: `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4`.
- Passed: PowerShell parser check for the new probe runner.
- Measured runs are retained under `logs/slippi-game-tick-poc-*`; the decisive negative run is `logs/slippi-game-tick-poc-frozen-scheduler-restored`.
- Gate result: control-flow feasibility **PASS**; one-tick exact equivalence **FAIL**; multi-tick, side-effect, performance, and WAN gates **NOT RUN** because promotion stops at the first correctness failure.

## 2026-08-01 Slippi architecture review and historical recommendation

### Historical judgment at the start of the review (superseded)

The user's assessment that the existing rollback path is not yet fully comfortable is supported by the repository evidence. The current exact path restores a lightweight checkpoint and then calls a full `nds->RunFrame()` for every corrected frame. The later exact-one-frame gate above supersedes the early `19-37ms` range: its affinity-isolated p50/p95/max are `17.442/20.626/21.543ms`. Snapshot compression alone cannot fix that result because replay `RunFrame()` itself reached `16.401ms` at p95.

At this point in the review, the recommended research target was a **Slippi-style game-loop rollback kernel**, not another full-emulator savestate variant and not a return to Plan-D semantic state repair. The transferable idea was to restore broad game RAM and re-enter only the title's gameplay update loop inside the current display frame while suppressing render, audio, input sampling, VBlank, network, and other one-shot side effects. The ROM-loop experiments above completed that bounded test and rejected it for production: depth 1 is slower than a normal replay `RunFrame()`, depth 2 misses budget, and depth 4 also fails the image gate.

The review led to the direct-call and ROM-side experiments documented above. The GUI default remains rollback disabled with `delay4/lead4`. This section records the reasoning that motivated those experiments; the authoritative current decision is maintained at the top of this document.

### What Slippi actually does

The review used the official Slippi Dolphin fork at commit `e7711b104b339a99385f2bb12b472d46140a7bc7` and the official Melee ASM repository at commit `fcf47f10dc244152c2ebaa3a9dec142ea42243b7`.

- **Input transport and prediction:** controller packets use an unsequenced ENet channel so reliable traffic does not head-of-line block them. Old unacknowledged inputs are carried forward, missing remote input is predicted from stale input, and predicted bytes are later compared with the real bytes. The rollback start is the earliest mismatching predicted frame.
- **Delay and bounded history:** the current fork defaults online input delay to two frames and allocates a fixed pool for at most seven rollback frames. It stalls or occasionally adjusts frame progression when one peer gets too far ahead instead of allowing unbounded speculation. Slippi is therefore not a zero-delay design.
- **Game-memory checkpoint:** `SlippiSavestate` copies selected Melee RAM ranges directly. Most Dolphin CPU/GPU/DSP/audio/full-system state serialization is commented out. Sound and XFB/VI regions are explicitly excluded, and guest-side netcode/input buffers can be preserved across a restore.
- **Game-loop-only resimulation:** the decisive optimization is in the Melee ASM patch. After restoring RAM and selecting historical/corrected inputs, the patch branches from the end of Melee's `updateFunction` back to the start of the game-engine loop until it catches up. Its own comment states that rollback loops without rendering frames. It does not invoke one complete Dolphin emulation frame for every correction.
- **Side-effect patches:** controller reads, interrupts, camera work, sounds, rumble, music, and stage-specific behavior receive explicit rollback handling. Slippi is therefore a game-integrated implementation, not a generic Dolphin savestate feature.
- **Desync and pacing:** finalized frames carry checksums, while frame skip/advance and small speed corrections keep peers near the same timeline and reduce consistently one-sided rollbacks.

Primary sources:

- [Slippi Dolphin fork](https://github.com/project-slippi/Ishiiruka/tree/e7711b104b339a99385f2bb12b472d46140a7bc7)
- [RAM-range capture/restore and preserved blocks](https://github.com/project-slippi/Ishiiruka/blob/e7711b104b339a99385f2bb12b472d46140a7bc7/Source/Core/Core/Slippi/SlippiSavestate.cpp)
- [Unsequenced input transport](https://github.com/project-slippi/Ishiiruka/blob/e7711b104b339a99385f2bb12b472d46140a7bc7/Source/Core/Core/Slippi/SlippiNetplay.cpp)
- [Prediction comparison and renderless rollback trigger](https://github.com/project-slippi/slippi-ssbm-asm/blob/fcf47f10dc244152c2ebaa3a9dec142ea42243b7/Online/Core/TriggerSendInput.asm)
- [Branch back to the Melee game-engine update loop](https://github.com/project-slippi/slippi-ssbm-asm/blob/fcf47f10dc244152c2ebaa3a9dec142ea42243b7/Online/Core/LoopEngineForRollback.asm)

### Mapping to NSMB MvL

| Slippi/Melee mechanism | Existing melonDS/NSMB evidence | Required NSMB-specific work |
| --- | --- | --- |
| Guest-side game update loop hook | Symbols exist for `ProcessList::execute()`, `ProcessManager::updateProcessLists()`, `Game::waitVBlankIntr()`, `Game::mainProcessTable`, and `Math::frameCounter` | The ROM-loop and guest history gate now pass `2-7` ticks under interpreter/JIT on the movement route; prove event routes |
| Curated game-RAM restore | `tinycorepreimage` already snapshots/restores Main RAM cheaply | Define a stable snapshot boundary and preserve packet bridge/input history and emulator-owned control state |
| Historical/corrected inputs inside guest | Runtime ARM9 patching and the packet bridge already write deterministic local/remote inputs | Prevent normal pad/Wi-Fi/input sampling from overwriting replayed inputs during catch-up |
| Skip render and one-shot effects | Current resim can skip GPU presentation, but still runs `NDS::RunFrame()` | Bypass or deduplicate display-list submission, VBlank/IRQ/DMA/timers, sound commands, ARM7 interactions, and network writes during extra game ticks |
| Fixed rollback window and pacing | Input timeline, prediction, mismatch detection, windowing, waits, and frame-lead control already exist | Retain these pieces; measure whether a practical `2-7` frame window is affordable after game-only resim |
| Finalized-frame checksum | Existing game-state comparison and semantic traces cover known MvL state | Promote a deterministic digest over every Main RAM page changed by a normal gameplay tick plus critical emulator-visible state |

The symbols and patch infrastructure made a bounded experiment possible, but the direct-call result proved that `ProcessList::execute()` alone is not the correct tick. The ROM-side loop preserves the native ordering and passes the current seven-tick semantic/recovery check under JIT. VBlank IRQs, DMA, timers, GPU FIFOs, ARM7 sound/Wi-Fi work, and untested event routes remain its current coupling risks.

### Required proof before implementation

1. **Game-tick equivalence gate:** from the same fixed-seed checkpoint and input, compare one normal `NDS::RunFrame()` with one candidate guest game-update tick. Compare every Main RAM page changed by the normal path, actor/process lists, player/star/block/item state, globals/RNG, and relevant GPU/audio command state. Repeat across movement, contact, item, death/respawn, result, and restart events.
2. **Multi-tick equivalence gate:** run `2-7` candidate ticks without intervening render/VBlank side effects, then compare against the same number of normal frames. A one-tick pass is insufficient because queues and timers can diverge only after several iterations.
3. **Forced-mismatch performance gate:** inject a known one-frame prediction error and measure restore plus game-only correction. Promotion requires rollback work to fit within the display budget; initial target is `p95 < 8ms` and no rollback-induced frame above `16.7ms` in the practical routes.
4. **Side-effect gate:** verify no duplicate sound, rumble, packet, item spawn, collision event, or graphics command, and no missed/extra IRQ, DMA, timer, or ARM7-visible event.
5. **WAN correctness gate:** only after the local equivalence gates pass, run the current packet-loss/jitter suite with a `2-7` frame window and finalized-frame digests.

If the first two gates show that hardware evolution is required for each NSMB gameplay tick, the Slippi form is not viable without a much larger DS scheduler split. In that case the practical fallback remains modest input delay plus rare full-frame correction; Plan-D remains a game-specific semantic synchronization alternative, not exact rollback.

### Verification status and next action

- Completed: reviewed `codex/rollback` history and the two prior rollback evaluation tasks; both support the measured full-`RunFrame()` cost and the lack of a fully comfortable exact path.
- Completed: inspected current `NsmbRollbackRuntime.cpp`; resimulation still invokes `nds->RunFrame()` once per corrected frame even when presentation is skipped.
- Completed: inspected the two official Slippi repositories and mapped their responsibilities to existing NSMB symbols/runtime components.
- Completed after this review: implemented and ran the diagnostic game-tick-equivalence harness described in the section above.
- Result: the direct update-list route passed control-flow feasibility but failed exact equivalence. Do not build the network-facing rollback mode from this boundary.
- Completed follow-up: implemented the ROM-side loop and renderless A/B recovery probe. The later performance and visual gates above reject connecting it to network-facing rollback.

## 2026-07-15 input prediction runtime refactor

Rollbackのremote input予測状態を`NsmbMvlNetplayRuntime.cpp`のflat field群から既存の`NsmbInputTimeline`へ移した。`PredictionRuntime`が予測map、最終確定入力、prediction/probe/mismatch counter、pending rollback frameと観測frameを所有し、確定入力優先、直前予測の引継ぎ、後着mismatch時の予測tail無効化、過去frameだけをrollback対象にする既存semanticsをROM不要contract testで固定している。checkpoint保存・復元は引き続き`NsmbRollbackStore`の責務であり、入力予測との混在は避けた。

固定seed・packet loss付き1,250-frame回帰は`logs/refactor-input-prediction-fast`と`logs/refactor-input-prediction-rollback`でpassした。後者は`tinycorepreimage`のcheckpoint保存coverage、host/client semantic trace、applied input、既存goldenの一致まで確認しており、この変更によるrollback policyやplayability評価の変更はない。

## 2026-07-15 rejected backend cleanup

The historical sections below retain the experiments and measurements that led to the current design, but `nsmbranges`, `nsmbcoreranges`, `nsmbtinycore`, and `arm9ram` are no longer supported runtime backends. They had failed correctness checks or served only as negative controls, and no current GUI path, refactor regression tier, or practical rollback candidate selected them. Their dedicated fixed/dynamic NSMB range scanner, ARM9-only format, diagnostics, config variables, and launcher/sweep branches were removed during the `NsmbNetplayPoC` refactor.

The retained runtime backends are `savestate`, `corelite`, `coresparse`, `coredelta`, `coreframedelta`, `corepreimage`, and `tinycorepreimage`. The GUI rollback toggle still selects `coredelta`; the refactor rollback golden and practical candidate use `tinycorepreimage`. Old command lines in later historical sections are evidence records, not currently runnable recommendations.

## 2026-06-07 GUI rollback settings exposure

Current GUI default remains the non-rollback input-delay path:

- rollback: disabled
- `InputDelayFrames`: `4`
- `InputMaxFrameLead`: `4`

The GUI battle settings now exposes `InputDelayFrames`, `InputMaxFrameLead`, and rollback enable/disable. Toggling rollback on sets `InputDelayFrames=2` and `InputMaxFrameLead=2`; toggling it off restores `4/4`.

When rollback is enabled from the GUI, melonDS is launched with coredelta rollback envs (`MELONDS_NSML_ROLLBACK=1`, backend `coredelta`, window `64`, checkpoint interval `8`, resimulation enabled, delta keyframe interval `30`, Main RAM page size `256`). This is an exposed experimental path for comparing delayed rollback against the current GUI default, not a new playability claim.

Current verification status:

- Passed: `cargo test --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml`.
- Passed: `cargo clippy --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml --all-targets -- -D warnings`.
- Passed: `corepack pnpm run ci` in `tools\nsmb-mvl-gui`.
- Passed: `corepack pnpm run ci` in `tools\nsmb-signaling-server`.
- Pending: manual GUI play comparison of default `delay4/lead4` vs rollback `delay2/lead2`.

## 2026-06-07 current status - practical tinycorepreimage candidate

Current practical rollback candidate is `tinycorepreimage-rbwait1500-window32`:

- backend: `tinycorepreimage`
- input delay: `0`
- input max frame lead: `2`
- rollback same-frame input wait: `1500us`
- rollback window: `32`
- checkpoint interval: `1`
- JIT enabled

Completed in the latest pass:

- Fixed a preimage shadow bug after rollback resimulation. The frame-delta shadow is now refreshed at the current frame after resim, not at the restored checkpoint frame. This removed a class of false preimage chains that later produced `chain missing`.
- Strengthened rollback history pruning so retained checkpoints also keep every base/preimage chain required by currently kept checkpoints and by the active frame-delta shadow. This prevents pruning a base frame that is still needed for restore.
- Added rollback integrity detection to `scripts/run-nsmb-mvl-practical-rollback-suite.ps1`; `cannot resimulate`, missing checkpoint, missing delta chain, restore failure, and rollback failure logs are now classified as `rollback-fail` instead of being hidden behind generic perf/mismatch failures.
- Added the `tinycorepreimage-rbwait1500-window32` and `tinycorepreimage-rbwait1500-bundle8` suite candidates. `bundle8` is not promoted because it improved some transport timing but introduced correctness failures.
- Made FPS spike phase tracing opt-in through `-FpsSpikeTrace` for split smoke and stopped forcing heavy trace/perf breakdown in the manual low-latency wrapper. Normal manual rollback runs are lighter by default, while active frame timing still records avg/max/over33ms.
- Fixed a result/restart bug: the MvL auto-restart checkpoint is saved before the packet-bridge JIT helper patch, so restoring it after result removed the patch from Main RAM while `PacketBridgeJitHelperPatchApplied` still stayed true. Auto restart now clears that applied flag after savestate restore, causing the helper patch to be reapplied on the next frame. This fixed second-game remote input not affecting the peer.
- Updated `secondgame` practical route to use `MvlLives=3`; with endless lives it only accumulated synchronized deaths and never reached result.

Verification:

- Build passed: `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4`.
- PowerShell parse passed for the touched rollback suite/manual/smoke scripts.
- `logs/codex-practical-suite-window32-sixroute-jitpatchfix-20260607/20260607-021914/summary.csv` passed all six current practical routes:
  - `stocktouch`: avg `18.371/18.370ms`, max `52.904/45.171ms`, over33 `3/7`
  - `chaos`: avg `17.317/17.315ms`, max `40.648/48.909ms`, over33 `3/7`
  - `death`: avg `17.305/17.304ms`, max `44.139/39.782ms`, over33 `4/2`
  - `contact`: avg `17.076/17.075ms`, max `40.459/40.216ms`, over33 `3/3`
  - `dualstresslong`: avg `17.530/17.530ms`, max `44.751/42.764ms`, over33 `9/6`
  - `secondgame`: avg `16.778/16.778ms`, max `46.982/47.397ms`, over33 `4/5`
- The second-game fix was first isolated in `logs/codex-practical-suite-window32-secondgame-jitpatchfix-20260607/20260607-021551`: helper patch logs appear at frame `870` and again at frame `4054` after auto restart, and the route reaches the second result without gameplay mismatch.
- A contaminated full-suite run, `logs/codex-practical-suite-rbwait1500-window32-postprune-20260607/20260607-012841`, showed much worse averages (`24-30ms`). It is not used as the current baseline because later single and full retakes under clean conditions passed with normal frame times. Keep using repeated retakes when a run shows global slowdown across every route.

Current conclusion:

- This candidate is now much closer to actual playability than the previous state: standard movement/item-touch, chaos input, death/respawn, player contact, long dual move/jump/dash, and result/restart into a second game all pass with practical FPS and no detected rollback integrity failure.
- The remaining rollback cost is still dominated by one-frame resimulation when a prediction miss occurs. Checkpoint save/restore is not the main blocker: recent runs show normal checkpoint saves around `1.6-2.4ms`, while one-frame correction can still spend roughly `19-37ms` depending on route and machine load.
- `luigistar` and `mariostarleft` are not current correctness failures; their latest run did not collect a star on either peer and therefore failed the event requirement symmetrically. They need better input coverage before they can prove dropped-star/star-pickup behavior.

Current blocker / caveat:

- Automated coverage still does not fully represent the user's manual reports around dropped stars, block break persistence, 8-coin item identity, and arbitrary complex contact. The six-route suite is a stronger baseline, not a final proof of comfortable human play.
- Manual play should use the low-latency tinycorepreimage/window32 path, but any new manual desync needs a trace route that captures the concrete event class instead of only checking final result.

Next actions:

- Add or repair deterministic routes for star pickup/drop/recover, block break state, and 8-coin item spawn identity. Treat those as gameplay-state coverage gaps, not as isolated one-off examples.
- Keep `tinycorepreimage-rbwait1500-window32` as the current candidate and reject `bundle8` unless a later correctness proof changes the input history semantics.
- Keep result/restart in the practical promotion matrix, but do not make the shorter default suite too slow unless needed; run the six-route matrix before claiming a playable milestone.
- Continue watching both average FPS and sudden frame drops (`maxFrameMs`, `over33ms`, consecutive slow frames). Average-only checks are insufficient.

## 2026-06-06 retained status - tinycorepreimage rollback profiling

At this retained checkpoint, the implementation direction had moved to rollback with lightweight checkpoints, not the earlier Plan-D actor/global/world snapshot path. The candidate under profiling was `tinycorepreimage`: frame-local Main RAM reverse preimages plus `DoRollbackTinyCoreSavestate` with `tinyFlags=0x241`.

Completed in the current pass:

- Added finer FPS-spike instrumentation to `src/frontend/qt_sdl/NsmbNetplayPoC.cpp`. `NSMB BeforeHookPhaseSpike` now splits the pre-frame hook into `probeRestoreMs`, `jitPatchMs`, `rollbackMs`, packet-bridge setup, checkpoint, scratch, network, and wait buckets. `NSMB PacketBridgeScratchSpike` further splits scratch writes into network, throttle, remote wait, and write time. New spike lines are flushed immediately so forced process termination is less likely to lose the cause.
- Updated `scripts/run-nsmb-mvl-split-local-input-smoke.ps1` to suppress CP15 `PU region` debug spam for tiny rollback backends, pass the same tinycorepreimage env as the manual wrapper (`tinyFlags=0x241`, JIT-reset skip, resim render skip), and enable `-RollbackResimulate` by default whenever `-Rollback` is requested. The previous split-smoke runs that omitted `-RollbackResimulate` could detect prediction mismatches without actually correcting them.
- Updated `scripts/run-nsmb-mvl-manual-local.ps1` so `-LowLatencyRollback -RollbackBackend tinycorepreimage` defaults to `InputMaxFrameLead=2`, rollback input wait `1500us`, checkpoint interval `1`, network pump `50us`, `tinyFlags=0x241`, JIT-reset skip, resim render skip, and PU debug suppression.
- Added `scripts/run-nsmb-mvl-practical-rollback-suite.ps1` to run practical rollback gates across stock-touch, chaos, death/respawn, player-contact, and long dual move/jump/dash routes. The suite records avg/max frame time, `over33ms`, consecutive slow frames, stall status, and game-state mismatch status in `summary.csv`.
- Added `GameStateTraceStartFrame` / `GameStateTraceEndFrame` pass-through to `scripts/run-nsmb-mvl-split-local-input-smoke.ps1` so dense diagnostic traces can focus on gameplay windows instead of comparing pre-game uninitialized rows.
- Added an experimental `-RollbackSkipIntermediateResimCheckpoints` switch to the split smoke wrapper and `MELONDS_NSML_ROLLBACK_RESIM_SKIP_INTERMEDIATE_CHECKPOINTS` in the PoC. It is intentionally not enabled by default; forced-prediction tests below show that skipping intermediate re-saves can break correctness.
- Build passed: `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4`.
- PowerShell parse passed for the touched manual and split smoke scripts.

Verification:

- `logs/codex-tinycorepreimage-phaseprobe-start50-2300-20260606`: `RollbackInputWaitUs=2500`, `InputMaxFrameLead=1`, game-state comparison enabled, 2300F passed. This avoided frequent rollback but active FPS was only about `46.18/46.19`; high-frequency remote input wait timeouts were the dominant cost. One-frame corrections measured about `29.011ms` host and `26.250ms` client total.
- `logs/codex-tinycorepreimage-phaseprobe-wait0-lead2-2300-20260606`: `RollbackInputWaitUs=0`, `InputMaxFrameLead=2`, game-state comparison enabled, 2300F passed. Active FPS improved to about `55.17/55.21`, throttle was essentially gone, and checkpoint save averaged about `1.37ms`. At frame `2280`, host/client had `19/19` correction ops; restore averaged `3.751/3.952ms`, resimulated `RunFrame` averaged `13.630/15.182ms`, resim checkpoint re-save averaged `1.522/1.560ms`, and total correction averaged `27.236/30.722ms`.
- `logs/codex-tinycorepreimage-suppressedpu-wait0-lead2-2300-20260606`: same route after script-level PU debug suppression passed. `PU region` spam was removed. Active FPS was about `54.78/54.76`; max frame was `78.139/68.791ms`, still caused mostly by rollback/resimulation windows, with one client scratch spike from throttle (`27.558ms`).
- `logs/codex-tinycorepreimage-stocktouch-wait0-lead2-3200-20260606`: stock-touch route with host move/jump/dash and client stock-item touch passed 3200F with game-state comparison. Active FPS was about `49.91/49.87`; max frame was `103.635/71.572ms`, `over33ms=40/48`. This broadens correctness confidence, but it shows complex routes still have visible spikes even when remote waits are gone.
- `logs/codex-baseline-stocktouch-allowjit-delay0-lead2-1400-20260606`: JIT-enabled, rollback-disabled baseline passed the early stock-touch comparison region. Active average was `16.655/16.654ms`, max `25.749/25.249ms`, and `over33ms=0/0`. This confirmed that later 50ms averages were an artifact of testing with JIT disabled, not normal play.
- `logs/codex-tinycorepreimage-stocktouch-allowjit-resimdefault-wait0-lead2-3200-20260606`: after fixing split-smoke resim defaults, JIT-enabled tinycorepreimage passed 3200F with game-state comparison and `RollbackSettleFrames=30`. Active average was `17.028/17.020ms`, max `56.939/56.545ms`, `over33ms=19/16`; rollback spikes were in `rollbackMs`, while checkpoints stayed around `1.2-1.7ms`.
- `logs/codex-tinycorepreimage-chaos-allowjit-resimdefault-wait0-lead2-3200-20260606`: JIT-enabled chaos route passed 3200F with game-state comparison and `RollbackSettleFrames=30`. Active average was `17.000/16.994ms`, max `62.499/68.794ms`, `over33ms=24/20`. The largest after-start spikes were rollback/resimulation (`rollbackMs` up to about `45.4/51.8ms`), not checkpoint save or packet scratch.
- `logs/codex-tinycorepreimage-chaos-allowjit-resimdefault-wait0-lead2-4200-20260606`: the same chaos route passed 4200F. Active average was `17.050/17.056ms`, max `95.879/69.156ms`, `over33ms=39/35`. This keeps correctness confidence up but confirms that rare visible spikes can still reach about `96ms`.
- `logs/codex-tinycorepreimage-death-allowjit-resimdefault-ignoreinput-skipmove-wait0-lead2-3600-20260606`: Luigi death/respawn-oriented route passed 3600F with game-state comparison, death and moving-hazard progress checks, and input-field comparison ignored. Active average was `17.108/17.092ms`, max `50.349/42.040ms`, `over33ms=6/5`. The skipped movement probe is intentional because the client-side death script keeps Luigi mostly stationary.
- `logs/codex-tinycorepreimage-chaos-predprobe10-allowjit-resimdefault-wait0-lead2-2600-20260606`: diagnostic forced-prediction chaos route passed 2600F with `RollbackPredictionProbeModulo=10`, limit `80`. Active average was `18.260/18.286ms`, max `59.209/71.206ms`, `over33ms=82/81`. This is a stress test for rollback frequency, not a normal-play promotion gate.
- Rejected experiment: `logs/codex-tinycorepreimage-chaos-predprobe10-skipresimckpt-allowjit-resimdefault-wait0-lead2-2600-20260606` skipped intermediate checkpoint re-saves during resim and failed at frame `1710` (`playerActor0X` mismatch). The intermediate checkpoints are therefore part of the correctness mechanism under repeated rollback, not just removable overhead.
- Rejected experiment: `logs/codex-tinycorepreimage-chaos-predprobe10-resimdelay2-allowjit-resimdefault-wait0-lead2-2600-20260606` passed but worsened spikes: active max `83.378/82.271ms`, `over33ms=88/92`, with transient position mismatches that only settled later. Delaying correction coalesces work but makes the eventual correction heavier.
- Short same-frame wait experiment: `logs/codex-tinycorepreimage-chaos-predprobe10-rbwait500-allowjit-resimdefault-lead2-2600-20260606` passed the same forced-prediction route. It reduced spike counts to `over33ms=65/62` and capped max around `58.827/57.355ms`, but average rose to `18.458/18.460ms` due to `~1ms` remote waits on many frames. `rbwait250` was not reliable in the same stress and failed at frame `2130` with `playerActor0X` mismatch.
- `logs/codex-tinycorepreimage-resultrestart-allowjit-resimdefault-wait0-lead2-12000-20260606`: existing repeat-result input route did not reach result scene by 12000F, so the result/restart gate failed. Host/client final game-state rows matched exactly (`sceneCurrentSceneID=0x3`, `sceneNextSceneID=0x181`, lives `3/3`, battle stars `0/0`, deaths `0x17/0x5`), so this is currently an input-scenario coverage blocker rather than a tinycorepreimage desync. Performance stayed good for the long run: active average `16.748/16.744ms`, max `67.083/69.989ms`, `over33ms=29/27`.
- Practical-suite precheck rejected the old `coredelta-baseline` as a playability candidate despite its oracle value: `logs/codex-practical-suite-baseline-20260606` failed stock-touch, chaos, and death with max frame spikes around `151-324ms`.
- `tinycorepreimage-rbwait1000` improved over wait0 but was still marginal: `logs/codex-practical-suite-rbwait1000-extended-20260606` passed stock-touch and death, and `logs/codex-practical-suite-rbwait-chaos-20260606` passed chaos at `1000us`, but the added `dualstresslong` route later exposed transient input-state mismatches around rollback correction frames.
- Dense diagnostic run `logs/codex-rbwait1000-dualstress-trace1-1600b-20260606` showed those short mismatches were not missing Main RAM coverage: the mismatching rows had different `inputPlayer*Held` values for one frame and settled on the next frame. Example: at frame `1141`, host had `inputPlayer0Held=0x811` while client still had `0x810`; frame `1142` matched again.
- `tinycorepreimage-rbwait1500` is the current practical candidate. `logs/codex-practical-suite-rbwait1500-20260606/20260606-230223` passed stock-touch (`18.346/18.346ms`, max `59.030/58.929ms`), chaos (`17.997/17.996ms`, max `51.196/41.169ms`), death (`17.220/17.218ms`, max `40.202/40.141ms`), and contact (`17.039/17.039ms`, max `40.674/41.405ms`). One `dualstresslong` suite run had a non-rollback scratch/PacketBridge spike, while a direct repeat `logs/codex-rbwait1500-dualstresslong-7200-20260606` passed 7200F at `17.757/17.757ms`, max `64.419/63.123ms`.
- `tinycorepreimage-rbwait2000` reduced one dualstress run (`logs/codex-rbwait2000-dualstresslong-7200-20260606`, max `57.824/53.977ms`) but was worse on other play routes: `logs/codex-practical-suite-rbwait2000-20260606/20260606-231231` failed chaos/death with `runFrameMs` spikes around `100ms`. It is not the current default.
- Rejected input-lead experiments: `tinycorepreimage-rbwait1500-lead4` averaged around `28-29ms` and failed all five practical routes in `logs/codex-practical-suite-rbwait1500-lead4-20260606`; `tinycorepreimage-rbwait1500-lead8` passed stock-touch but made chaos/death worse and stalled contact/dualstresslong in `logs/codex-practical-suite-rbwait1500-lead8-20260606`. `InputMaxFrameLead=2` remains the practical setting.

Current conclusion:

- The stale 2026-06-02 conclusion that the comparison-enabled `tinycorepreimage` route stopped around frame `1453` no longer represents the current branch. Re-runs now complete 2300F under game-state comparison.
- JIT must be enabled for practical FPS assessment. JIT-disabled split-smoke runs can sit around `50ms` per active frame and should not be used as the normal-play performance baseline.
- Checkpoint storage is light enough for the current candidate: normal checkpoints are roughly `250KB` on the JIT allow route, save is usually about `1.2-1.7ms`, and restore is about `3.4-5.3ms` on these routes.
- The remaining visible frame drops are not dominated by checkpoint bytes. With JIT enabled, average frame time is usually near `17-18ms`, but rare frames can still spike from rollback resimulation, PacketBridge scratch throttle, or emulator `RunFrame`/JIT work.
- Input policy matters more than snapshot bytes now. `lead=2` is still required; widening to `lead4` or `lead8` made practical routes worse or stalled. A bounded same-frame remote-input wait around `1500us` is currently the best tradeoff found: it reduces one-frame speculative input mismatches without becoming input delay.

Current blocker / caveat:

- `tinycorepreimage` is promising but not promoted. With `rbwait1500/lead2`, stock-touch, chaos, death/respawn, contact, and a direct 7200F dual move/jump/dash route pass, but repeatability is not yet good enough for "comfortable real play" because rare PacketBridge throttle and `RunFrame` spikes still occur.
- Existing repeat-result input scripts no longer prove result/restart coverage under the current direct-MvL setup: by 12000F they stay synchronized but never enter result. A new deterministic result/restart route is needed before promoting the backend.
- Full write-barrier coverage is still not proven. The current page-comparison/preimage path is correctness-oriented; replacing it with write tracking should wait until more routes pass.

Next actions:

- Keep `rbwait1500, lead=2` as the current tinycorepreimage manual/practical-suite default and use frame-spike/stall/consecutive-slow gates, not only average FPS.
- Keep `-AllowJit` on for practical automated FPS tests, and keep `-RollbackResimulate` enabled for rollback correctness tests.
- Next implementation focus is PacketBridge/InputNetplay throttle behavior under rollback: avoid long scratch-throttle waits that destroy FPS, while keeping `InputMaxFrameLead=2` semantics stable enough to avoid runaway rollback.
- Run the same backend on block/item, actual result/restart, longer manual-like routes, and repeated practical-suite runs. Treat forced all-frame prediction-probe tests as diagnostic stress, not as a promotion gate.
- Rework the repeat-result input route so it reaches result scene deterministically; the current script proves long synchronized death/life churn but not restart coverage.
- Keep intermediate checkpoint re-saves during resim unless a different correctness proof replaces them; the skip experiment desynced under repeated rollback.
- If spikes remain too visible, prefer reducing long PacketBridge throttle waits and rollback frequency through a small bounded same-frame wait or smarter prediction over deleting checkpoint bytes. Current measurements show checkpoint save/restore is already light enough; resimulation, scratch throttle, and occasional `RunFrame` spikes are the practical blockers.

## 2026-06-02 retained diagnostic status - Plan-D actor/global snapshot path

This section records the 2026-06-02 Plan-D experiment. It was not promoted to the production correctness route. As of 2026-07-15, all supplemental Plan-D correction branches described below—including item spawn, generic actors, effects, players, Big Star, and moving hazards—have been removed. A matched 1,100-frame A/B run produced identical zero drift with the last two corrections enabled and disabled; after removal, a second 1,100-frame real-ROM run again compared Big Star and moving hazards seven times each with zero X/Y drift. Observation-only world traces and drift gates remain independently available.

- Historical player packet: `WirePlayerState`, 168 bytes, carried base actor fields plus optional player globals. Its wire type, history, send/apply path, config, and runner options were removed on 2026-07-15 after repo-wide reference and current-scenario audits found no active consumer. Diagnostic player snapshots and the general `STATE_APPLY` remote-player route remain.
- Historical world packets: `WireWorldState` was reduced from 520 to 120 bytes before being removed; `WireMovingHazardState` was a separate 424-byte packet for four actors. Their wire types, stores, caches, send/apply paths, prediction, config, and runner options were removed on 2026-07-15 after the A/B and post-removal natural-sync checks.
- Historical host-authoritative effect packet: `WireWorldEffectState`, 760 bytes, carried up to four active fixed Effect slots from `0x021C3268 + 0x1D4*i`. The packet, send/apply path, and RAM writer were removed on 2026-07-15; local effect-internal tracing remains available.
- Historical generic actor packet: `WireWorldActorSnapshotState`, 1688 bytes, carried up to sixteen non-player, non-manager process-list actors. It carried object ID plus compact transform/velocity/common actor fields, while the receiver applied transform/velocity only. The wire type, GUID mapping, send/apply path, config, and runner switch were removed on 2026-07-15.
- Historical launcher preset: `scripts/run-nsmb-mvl-manual-local.ps1 -PlanDActorSnapshot` selected the experimental correction stack and related latency defaults. The switch and its tuning block were removed with the last correction packets.
- Lightweight gameplay liveness is available through `MELONDS_NSML_GAMEPLAY_HEARTBEAT_INTERVAL` / `-GameplayHeartbeatInterval`. It logs low-frequency player transforms and object lifecycle counts so the analyzer can detect in-game plateaus even when the emulator frame heartbeat keeps advancing.
- Gameplay heartbeat now also emits compact active object IDs/settings (`activeIds=`), and the analyzer reports both raw host-only/client-only active object sets and a gameplay-significant set that ignores known local-role objects such as `StageFX(0x012)` and `MvsLObject267(0x10B)`. This is the current main diagnostic for complex manual desyncs where the game keeps running but actors are missing, duplicated, or deactivated differently.
- Env/script switches:
  - Historical only: `MELONDS_NSML_PLAYER_STATE_SYNC`, `MELONDS_NSML_PLAYER_STATE_APPLY`, `MELONDS_NSML_PLAYER_STATE_GLOBALS`, interval/prediction envs, and the matching split/lan runner options enabled the supplemental player route. They were removed on 2026-07-15.
  - Historical only: `MELONDS_NSML_WORLD_STATE_SYNC`, apply/skip, interval/prediction, and actor-rescan envs enabled the Big Star and moving-hazard corrections. They and the matching runner options were removed on 2026-07-15.
  - Historical only: `MELONDS_NSML_WORLD_STATE_SPAWN_ITEM=1` enabled the item-specific client spawn experiment. The env switch and `-PlanDActorSnapshot` propagation were removed on 2026-07-15.
  - Historical only: `MELONDS_NSML_WORLD_STATE_APPLY_EFFECTS=1` enabled the compact Effect slot snapshot. The apply/skip envs and launcher propagation were removed on 2026-07-15.
  - Historical only: `MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT=1` enabled the generic process-list actor snapshot. The env/config and launcher propagation were removed on 2026-07-15.
  - `MELONDS_NSML_WORLD_STATE_TRACE_ACTOR_INTERNALS=1` and `MELONDS_NSML_WORLD_STATE_TRACE_EFFECTS=1` are diagnostic-only tracing modes for ROM/memory analysis. They are not part of normal manual play.
- Historical remote world and moving-hazard samples rejected older packet frames instead of allowing late unreliable packets to overwrite a newer sample.
- Historical after-frame artifact correction synced first and applied only a fresh same-frame moving-hazard sample. The player route likewise applied a fresh-only sample there and used the latest eligible sample before frame execution.
- Historical player packet details: it carried actor transform, velocity, action/subaction/physics flags, damage cooldown, transition/collision/environment flags, compact runtime bytes, and optional event-only life/death/pipe/star globals. During transitions it applied only minimal visible/defeated bytes. This entire supplemental correction path is no longer present.
- Current fixed-size packets are classified before the input-bundle branch so `WireNSMLPacket` and `WireGameState` reach their exact handlers. The removed World packet sizes now follow the ordinary input-bundle-candidate classification contract.
- Generated direct-MvL ROMs now patch the stage-object activation scan to use player `0` on both peers. The local-player-1 scan could skip early off-camera actors such as the first Goomba, which matches the user-reported "enemy appears on only one side" class of desync better than a runtime clone-all approach.

Verification:

- Build passed: `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4`.
- Historical generic actor snapshot validation (implementation removed 2026-07-15):
  - Historical manual freeze logs are no longer misclassified as OK: `logs/nsmb-mvl-manual-local-20260602-175043` and `logs/nsmb-mvl-manual-local-20260602-175135` now analyze as `failed` because the wrapper reported missing host/client frame limits.
  - `logs/codex-pland-actor-snapshot-lead4-stress-3600-20260602`: no-trace move/dash/jump stress passed with actor snapshot and lead `4`; host/client active avg `16.782/16.781ms`, max `29.305/27.061ms`, and `over33ms=0/0`.
  - `logs/codex-pland-actor-snapshot-heartbeat-lead4-stress-4200-20260602`: gameplay-heartbeat stress passed; analyzer status `ok`, gameplay plateau `1/1`, host/client active avg `17.048/17.047ms`, max `39.255/37.939ms`, max consecutive slow frames `1/1`.
  - `logs/codex-pland-actor-snapshot-trace-lead4-stress-1800-20260602`: trace confirmed client-side generic actor application, typically `2-5` actors per sampled row, while staying below `33ms`.
  - `logs/codex-pland-actor-snapshot-lifecycle-lead4-stress-3000-20260602`: lifecycle gate passed with `host=18 client=18 shared=18` and no unexpected actor-count differences.
  - Rejected experiment: generic application of `StateType`, `Flags`, and common motion fields was too aggressive. `logs/codex-pland-actor-common16-heartbeat-lead4-stress-4200-20260602` stalled around frame `2040`, so the historical implementation stayed transform-only.
  - `logs/codex-pland-actor16-heartbeat-lead4-stress-4200-20260602`: 16-slot transform-only actor snapshot passed; analyzer status `ok`, gameplay plateau `1/1`, host/client active avg `17.724/17.724ms`, max `36.728/34.511ms`, and `over33ms=3/3`. This was measurably heavier than the previous 12-slot run and was not promoted.
  - `logs/codex-pland-actor16-heartbeatids-lead4-stress-1800-20260602`: heartbeat `activeIds=` output and object-diff analyzer path passed a short validation run; host/client active avg `16.667/16.665ms`, max `26.563/27.330ms`, `over33ms=0/0`, and heartbeat object diff stayed `0`.
  - `MvsLObject267(0x10B)` is now excluded from the generic world actor snapshot candidate set. A 6200F result/restart probe, `logs/codex-pland-actor16-heartbeatids-result-restart-6200-20260602`, showed raw heartbeat differences consisting only of local `StageFX(0x012)` settings and transient `0x10B`; significant diff stayed `0`. The run did not reach the second-game gate within 6200F, so it is a diagnostic route result rather than a promotion gate.
  - `logs/codex-pland-actor16-exclude10b-heartbeatids-stage0-2600-20260602`: movement-enabled validation passed after excluding `0x10B`; analyzer status `ok`, host/client active avg `16.849/16.848ms`, max `43.355/41.246ms`, max consecutive slow frames `1/1`. The new significant object-diff summary exposed a recurring `053:00000000` host-only moving-hazard lifecycle gap across `3/14` shared heartbeat rows (`firstSignificantActiveIdDiffFrame=2040`). This is the next concrete non-local actor drift to investigate.
  - Heartbeat now also prints `hazards=` with up to four moving-hazard GUID/position/velocity/state/flags tuples. Re-running the same 2600F route as `logs/codex-pland-hazarddetails-heartbeat-stage0-2600-20260602` passed with object diff `0`, Big Star drift `0/0`, moving-hazard max drift `2048/0`, active avg `17.190/17.192ms`, max `29.897/31.898ms`, and `over33ms=0/0`. The earlier `053` gap is therefore not a stable every-run divergence on this route, but future manual/long-run logs will now identify the missing hazard GUID and coordinates when it occurs.
  - Added asymmetric chaos input routes:
    - `tests/nsmb_us_direct_mvl_chaos_host.inputs`
    - `tests/nsmb_us_direct_mvl_chaos_client.inputs`
    These mix uneven left/right periods, jump, Y/B run buttons, short stops, and touch events to better approximate complex manual input.
  - First chaos stage `0` run, `logs/codex-pland-chaos-stage0-7200-20260602`, passed but exposed a client-only `Item(0x01F settings=0x00080000)` across three heartbeat samples. Adding the bidirectional `NeutralItem` slot alone was insufficient because older unreliable world packets could overwrite the newer `Found=1` sample.
  - After rejecting older world/effect/actor/hazard packets and keeping the bidirectional `NeutralItem` slot, the same chaos stage `0` route passed as `logs/codex-pland-chaos-neutralitem-newest-stage0-7200-20260602`: analyzer status `ok`, `01F:00080000` occurrences `0`, active avg `17.244/17.244ms`, max `39.852/46.189ms`, max consecutive slow frames `1/1`, Big Star drift `0/0`, moving-hazard max drift `2048/0`. Remaining significant object diff was a single `053:00000000` activation-boundary row at frame `2160`.
  - Chaos stage `1` also passed after the same fix: `logs/codex-pland-chaos-neutralitem-newest-stage1-7200-20260602`, analyzer status `ok`, active avg `17.151/17.151ms`, max `42.170/44.802ms`, max consecutive slow frames `1/1`, Big Star drift `0/0`, moving-hazard max drift `2048/0`. `01F` actors appeared with matching settings on both peers; no item-only significant diff remained.
  - Chaos stage `2` passed as `logs/codex-pland-chaos-neutralitem-newest-stage2-7200-20260602`: analyzer status `ok`, significant object diff `0`, active avg `17.775/17.773ms`, max `45.544/45.773ms`, max consecutive slow frames `1/2`, Big Star drift `0/0`, and no tracked moving hazard on this route. Stage `2` is a bit heavier (`over33ms=14/17`) but still did not show a rollback-style stutter or persistent actor drift.
  - Chaos stage `3` passed as `logs/codex-pland-chaos-neutralitem-newest-stage3-7200-20260602`: analyzer status `ok`, significant object diff `0`, active avg `17.150/17.150ms`, max `31.742/30.504ms`, max consecutive slow frames `0/0`, Big Star drift `0/0`, and `over33ms=0/0`.
  - Chaos stage `4` passed as `logs/codex-pland-chaos-neutralitem-newest-stage4-7200-20260602`: analyzer status `ok`, significant object diff `0`, active avg `18.719/18.719ms`, max `45.365/49.665ms`, max consecutive slow frames `2/2`, Big Star drift `0/0`, and no tracked moving hazard. This stage is the heaviest chaos route so far (`over33ms=17/27`) and should stay in the performance matrix.
  - A stricter 8400F chaos rerun exposed a real automatic detection failure before the latest fix: `logs/codex-pland-chaos-seed13579-stage0-8400-20260602` failed strict comparison at frame `930` on `movingHazardX`, and `logs/codex-pland-sync1-worldpred2-playerpred1-driftgate-stage0-8400-20260602` still failed the moving-hazard drift gate at frame `2130` (`dx=2048`) before the fresh same-frame hazard apply.
  - Fresh same-frame moving-hazard apply fixed the hazard gate for generated stages `0` and `1`: `logs/codex-pland-freshhazard-sync1-driftgate-stage0-8400-20260602` and `logs/codex-pland-freshhazard-sync1-driftgate-stage1-8400-20260602` both passed with Big Star drift `0/0`, moving-hazard drift `0/0`, and no significant active-object diff. Stage `0` active avg was `16.951ms`; stage `1` was `18.261ms`.
  - Fresh-only after-frame player apply then removed the remaining one-sample player drift on complex input. `logs/codex-pland-freshplayer-stage2-8400-20260602`, `logs/codex-pland-freshplayer-stage3-8400-20260602`, and `logs/codex-pland-freshplayer-stage4-8400-20260602` passed input-delay-`0` 8400F chaos validation with Big Star drift `0/0`, significant active-object diff `0`, and player drift `96/0`, `0/0`, and `0/0`. The stricter final fresh-only hazard/player reruns, `logs/codex-pland-freshonly-stage0-8400-20260602` and `logs/codex-pland-freshonly-stage1-8400-20260602`, also passed with Big Star drift `0/0`, moving-hazard drift `0/0`, significant active-object diff `0`, and player drift `2528/2432` and `0/0`.
  - The strict trace-enabled `freshonly` stage `0` run produced a `perf-fail` caused by the observer, not rollback or actor apply: `TraceGameState` took `215.729ms` at frame `4620`. Re-running the same stage/input without full CSV tracing as `logs/codex-pland-freshonly-stage0-notrace-8400-20260602` passed with analyzer status `ok`, active avg `17.542ms`, max `38.749/39.489ms`, max consecutive slow frames `1/1`, `over33ms=3/4`, and significant active-object diff `0`. Keep using targeted trace runs for drift and no-trace runs for FPS/spike promotion.
  - Latest available manual log before the active-ID heartbeat change, `logs/nsmb-mvl-manual-local-20260602-181856`, is still useful as a drift/stall example: analyzer reports `status=stalled`, host/client heartbeat frame `21960`, and gameplay heartbeat object divergence across `39/177` shared frames with `maxActiveDelta=3`, first at frame `1320` (`host=12/12/0/0 client=11/11/0/0`). New manual logs should now identify the concrete host-only/client-only active IDs at the first divergence.
  - Strict full game-state CSV equality is still too strict for this non-deterministic actor-correction path. `logs/codex-pland-actor-snapshot-gamestate-lead4-stress-2400-20260602` failed on a small moving-hazard X sample delta (`0x800`) at frame `960`; this should be treated as a comparison-timing caveat, not as a full freeze reproduction.
- First naive implementation used `ReadGameStateSample()` every frame and was too heavy: `logs/codex-playerstate-norollback-lead8-stress-seq-1600-20260602` ran only about `37.6fps` active.
- After replacing that with `FindPlayerActors()` plus direct player offset reads:
  - `logs/codex-playerstate-fastsend-lead8-stress-1600-20260602`: 1600F passed, host/client active FPS about `57.9`, max frame about `36ms`, `over33ms=2`.
  - `logs/codex-playerstate-fastsend-lead8-stress-2400-20260602`: 2400F passed, no rollback/resim, host/client active FPS about `51.6`, max frame `31.979/34.490ms`, `over33ms=0/2`, and both player actor X values moved.
  - `logs/codex-playerstate-interval2-predict1-lead8-stress-2400-20260602`: 2400F passed with send interval 2 and prediction 1, host/client active FPS about `54.5`, max frame `33.074/33.406ms`, `over33ms=0/1`.
- Added a dedicated actor-snapshot gate for `-SkipGameStateComparison` runs. It checks both directions of remote actor movement, optional host/client coordinate drift, active max frame, over33ms count, and consecutive slow frames.
- `scripts/analyze-nsmb-mvl-rollback-log.ps1` now parses hex trace fields correctly and only treats actor plateaus as freeze suspects while input is being held. It classifies the user-reported Plan-D-like manual freeze `logs/nsmb-mvl-manual-local-20260601-212956` as `abort/perf-fail`, and the baseline `logs/nsmb-mvl-manual-local-20260601-213213` as `ok`.
- Player actor base/GUID caching was added for the actor snapshot send/apply path so it does not scan all Main RAM every frame unless the cached actor becomes invalid.
- Cached actor snapshot verification:
  - `logs/codex-playerstate-cache-drift-gate-interval2-predict1-2400-20260602`: 2400F passed with movement/drift gate; host/client active FPS about `59.5`.
  - `logs/codex-playerstate-cache-drift-gate-interval2-predict1-4200-20260602`: 4200F passed with movement/drift gate; host/client active FPS about `59.5`, max frame `33.224/32.548ms`, `over33ms=0/0`, max drift X/Y `12352/16768`.
  - `logs/codex-playerstate-cache-luigi-death-3600-20260602`: Luigi death/respawn-style probe passed with player death and pipe visibility checks; analyzer status `ok`, active FPS about `59.6`, max frame `40.399/45.362ms`, `over33ms=4/4`, max consecutive slow frames `1/1`.
- Historical actor+global snapshot verification (implementation removed 2026-07-15):
  - Rejected earlier always-apply global snapshots: `logs/codex-playerglobal-cache-drift-gate-interval2-predict1-4200-20260602` and `logs/codex-playerglobal-directwrite-drift-gate-interval2-predict1-4200-20260602` ran around `48fps` with many `over33ms` frames.
  - Event-only globals with signed drift fixed passed normal movement: `logs/codex-playerglobal-events-optional-on-drift-gate-interval2-predict1-4200-20260602`, active avg about `16.79ms`, max `43.724/46.892ms`, `over33ms=4/5`.
  - A death/pipe route without transition-step minimal apply reproduced a bad stutter: `logs/codex-playerglobal-events-luigi-death-3720-20260602` hit `353.752/354.027ms` max frames around frame `2794` and is now classified as `perf-fail`.
  - With transition-step minimal apply, the same death/pipe route passed: `logs/codex-playerglobal-transition-step-min-luigi-death-3720-20260602`, active avg about `17.10ms`, max `39.388/38.823ms`, `over33ms=9/9`, max consecutive slow frame `1/1`.
  - With transition-step minimal apply, normal movement still passed: `logs/codex-playerglobal-transition-step-min-stress-4200-20260602`, active avg about `17.05ms`, max `44.636/42.051ms`, movement/drift gate passed.
- Result route using `RequireResultScene` passed with actor+global snapshot: `logs/codex-playerglobal-transition-step-min-result-9000-20260602`, active avg about `16.77ms`, max `46.907/45.751ms`, `over33ms=11/11`. The input file name suggested a star route, but trace review showed the result came from repeated fall deaths while star counter fields remained `0/0`. `RequireStarPickup` is not a valid assertion for this route.
- The split-input wrapper now exposes `-RequireSecondMvlGame`, `-RequireMvlGameCount`, and `-RequireMvlGameStages`. A result/restart route using the actor+global snapshot passed a second-game gate: `logs/codex-playerglobal-transition-step-min-secondgame-gate-9000-20260602`.
- Added `tests/nsmb_us_direct_mvl_star_collect_second_game_stress.inputs`: after result/restart it drives both players with simultaneous move, dash, jump, and direction reversals. `logs/codex-playerglobal-sustained-drift-gate-secondgame-stress-10000-20260602` passed 10000F, reached the second MvL game, moved both remote actors for 126 sampled rows, and passed movement/drift validation. Active avg was about `17.40ms`, max `56.405/54.940ms`, `over33ms=44/47`, and max consecutive slow frames `2/1`.
- The actor drift gate ignores dead/transition rows because transition-step minimal apply intentionally does not overwrite transform there. It also supports `-ActorSnapshotMaxConsecutiveDriftRows`; the 10000F route allows one transient sampled row but still fails sustained drift. The passing route observed max drift X/Y `24192/17024` and no sustained over-limit row.
- Historical manual command: `scripts/run-nsmb-mvl-manual-local.ps1 -PlanDActorSnapshot -AllowJit` enabled the Big Star/moving-hazard correction experiment and related tuning. It is no longer available; use normal manual play or the current rollback preset according to the scenario being tested.
- Historical player behavior: global writes were applied once per newly received player-state packet while actor transform prediction ran each frame. This code was removed with the supplemental player route.
- Post-change normal movement validation passed: `logs/codex-playerglobal-globals-once-normal-stress-4200-20260602`, active avg `17.608/17.605ms`, max `43.060/46.158ms`, max consecutive slow frames `1/1`, and movement/drift validation passed with max drift X/Y `12352/16768`.
- Post-change visible-window manual launch passed: `logs/codex-manual-pland-globals-once-launch-1800-20260602`, active avg `17.372/17.370ms`, max `28.831/31.461ms`, `over33ms=0/0`.
- Added a host-authoritative Big Star world snapshot and `-RequireWorldSnapshotSync` gate. The gate fails on Big Star actor drift and reports moving-hazard drift separately.
- Standard Big Star-only world snapshot validation:
  - `logs/codex-worldstate-star-gate-normal-stress-rerun-4200-20260602`: normal move/dash/jump stress passed, Big Star drift `0/0`, active avg `17.083/17.083ms`, max `43.936/40.535ms`, `over33ms=6/7`.
  - `logs/codex-worldstate-star-gate-luigi-death-3720-20260602`: death/respawn route passed, Big Star drift `0/0`, active avg `17.192/17.193ms`, max `41.988/42.378ms`.
  - `logs/codex-worldstate-star-gate-result-restart-6200-20260602`: result/restart into a second MvL game passed, Big Star drift `0/0`, active avg `17.402/17.403ms`, max `67.511/50.303ms`.
- Rejected naive moving-hazard correction as a standard-path feature:
  - `logs/codex-worldstate-hazard-only-normal-stress-4200-20260602` showed that transform-only apply was insufficient.
  - Adding hazard physics fields and active GUID cache refresh fixed one respawn boundary, but `logs/codex-worldstate-newest-rescan-combined-normal-stress-4200-20260602` showed that selecting one newest `0x0053` actor can still target the wrong simultaneous instance.
  - A low-frequency diagnostic scan showed that active hazard count is normally `1`, briefly `2` during replacement, and GUIDs can acquire a stable host/client offset even when motion matches.
- Added compact multi-instance moving-hazard correction:
  - Host sends up to four active `0x0053 settings=0` actors ordered by creation GUID. Client only applies when host/client active counts match.
  - Client now keeps a persistent `remote GUID -> local GUID` map across frames. When a lifecycle boundary introduces a new remote actor, it pairs the remaining local actor by nearest current position. This preserves identity across actor crossings and no longer assumes that host/client creation order stays identical.
  - Active-list refresh now traverses the NSMB process lists instead of scanning all 4MB Main RAM. The initial full-RAM scanner raised normal stress average frame time to `18.26ms`; process-list traversal reduced it to `16.93ms`.
  - `logs/codex-worldhazards-processlist-normal-stress-4200-20260602`: normal stress passed, Big Star drift `0/0`, moving-hazard max drift `2048/0`, active avg `16.928/16.929ms`, max `39.932/39.572ms`.
  - `logs/codex-worldhazards-processlist-luigi-death-3720-20260602`: death/respawn passed, moving-hazard max drift `2048/3072`, active avg `16.898/16.899ms`, max `42.804/48.631ms`.
  - `logs/codex-worldhazards-processlist-result-restart-6200-20260602`: result/restart into a second MvL game passed, moving-hazard max drift `4096/0`, active avg `16.908/16.908ms`, max `49.980/49.207ms`.
  - `logs/codex-pland-hazard-guidmap-stage0-stress-3100-20260602`: the new GUID map exercised lifecycle churn and stable host/client GUID offsets such as `43/44`, `44/45`, and `48/49`; moving-hazard max drift stayed `2048/0`, with max consecutive drift rows `0`.
  - `logs/codex-pland-hazard-guidmap-result-restart-6200-20260602`: result/restart passed after the GUID-map change, with Big Star drift `0/0`, moving-hazard max drift `4096/0`, manager/global agreement across `177` rows, active avg `16.829/16.829ms`, max `52.843/51.141ms`, and max consecutive slow frames `1/1`.
  - `logs/codex-pland-hazard-guidmap-luigi-death-3720-20260602`: death/pipe-respawn passed with hazard-progress and pipe-visibility gates enabled. Active-count lifecycle boundaries converged without sustained drift; moving-hazard max drift was `4096/0`, active avg `16.746/16.747ms`, max `39.574/39.730ms`, and max consecutive slow frames `1/1`.
  - `logs/codex-manual-pland-worldhazard-launch-1800-20260602`: visible-window `-PlanDActorSnapshot` launch completed with active avg `17.462/17.461ms`, max `31.970/30.764ms`, and `over33ms=0/0`.
- Added `-RequireMvlManagerGlobalSync`, an observation-only gate for selected MvL manager/global/stage-scene fields. It intentionally does not add runtime writes when the trace already agrees:
  - `logs/codex-mvlmanager-gate-result-restart-6200-20260602`: result/restart passed with `177` compared rows, Big Star drift `0/0`, moving-hazard max drift `4096/0`, active avg `16.800/16.801ms`, max `62.290/63.555ms`.
  - `logs/codex-mvlmanager-gate-repeat-result-threegame-12000-20260602`: three-game repeated result/restart passed with `371` compared rows, Big Star drift `0/0`, moving-hazard max drift `4096/0`, active avg `16.773/16.773ms`, max `47.360/45.596ms`.
- Real Big Star acquisition is now covered by a deterministic probe. `tests/nsmb_us_direct_mvl_luigi_star_right.inputs` depends on initial star placement, so the reliable condition is `-MvlMatchSeed 0x19FE5603`:
  - `logs/codex-pland-luigi-star-right-seed19fe5603-2600-20260602`: Luigi collected the real star with Plan-D snapshots enabled, Big Star drift `0/0`, moving-hazard max drift `2048/0`, active avg `16.867/16.869ms`, max `39.714/41.264ms`.
  - `logs/codex-pland-luigi-star-right-settle-seed19fe5603-3200-20260602`: the post-collection star counter and respawned star converged on both sides. This rejects the earlier apparent failure from a run whose random initial star was at `0x3c0000` instead of the probe-compatible `0x90000`.
  - `logs/codex-pland-hazard-guidmap-luigi-star-settle-3200-20260602`: the current moving-hazard GUID-map path also passed real Luigi star pickup and settle. Big Star drift stayed `0/0`, moving-hazard max drift was `2048/0`, manager/global agreement covered `77` rows, active avg was `17.346/17.346ms`, max `54.719/53.511ms`, and max consecutive slow frames `1/1`.
- The split wrapper now forwards `-MvlStage`, `-MvlSceneSettings`, `-MvlBigStars`, `-MvlLives`, `-MvlCourseMode`, and `-GenerateMvlConfiguredRoms`. Configured-ROM generation uses the unpatched default source `roms/nsmb-us.nds` through `-GenerateMvlSourceRom`.
- Plan-D stage variation matrix passed for all five courses with generated ROMs, move/dash/jump stress, player actor movement gate, Big Star drift gate, moving-hazard drift gate, manager/global gate, and frame-spike gate:
  - `logs/codex-pland-world-stage0-generated-stress-2400-20260602` through `logs/codex-pland-world-stage4-generated-stress-2400-20260602`.
  - Host/client active averages ranged from `16.918ms` to `17.676ms`; maxima ranged from `35.407ms` to `63.668ms`.
  - Courses `0` and `1` exercised the tracked `0x0053` hazard with max drift `2048/0`. Courses `2`, `3`, and `4` had no tracked `0x0053` hazard in the sampled route. All courses kept Big Star drift at `0/0`.
- Historical finding: the course `1` matrix trace exposed a persistent coin-global gap, and the supplemental `WirePlayerState` route was extended to write changed remote-player coin values. That correction was not promoted and was removed with the route on 2026-07-15; `vsCoinCount` shares `0x0208B37C` with `player0Coins`.
  - `-RequireMvlManagerGlobalSync` now includes `player0Coins`, `player1Coins`, and `vsCoinCount`, for `34` observed fields total.
  - `logs/codex-pland-coinsync-stage1-generated-stress-2400-20260602`: the reproducing course `1` stress route passed with fully converged coin fields, Big Star drift `0/0`, moving-hazard max drift `2048/0`, active avg `17.172/17.171ms`, max `42.176/42.178ms`.
  - `logs/codex-pland-coinsync-luigi-star-right-settle-3200-20260602`: real Luigi star acquisition still passed with the 34-field gate.
  - `logs/codex-pland-coinsync-result-restart-rerun-6200-20260602`: result/restart still passed with the 34-field gate, active avg `17.488/17.488ms`, max `57.761/52.862ms`.
- Camera/layout broad-diff classification is complete for the currently suspicious fields. `0x02092B4` tracks a coarse/quantized camera coordinate, `0x020CAF20` tracks local camera X at finer resolution, and `0x020CAF40` is a stage bound or brief local layout-transition value. They differ because each process has a local viewpoint and must not be synchronized.
- Added diagnostic-only actor lifecycle tracing:
  - `MELONDS_NSML_WORLD_STATE_TRACE_OBJECT_LIFECYCLES=1` prints GUID, object ID, settings, lifecycle state, type, flags, vtable, base, and `PosX/Y/Z` for actors reached through the ROM-analyzed NSMB process lists.
  - The LAN and split smoke scripts expose `-WorldStateTraceObjectLifecycles`, interval, start-frame, and end-frame parameters.
  - The original diagnostic used a full Main RAM scan and was intentionally not enabled by `-PlanDActorSnapshot`; dense 2-frame scans raised active averages to about `18-19ms`. It now reuses the process-list walker, so targeted lifecycle observation no longer requires a 4MB sweep.
  - `scripts/analyze-nsmb-mvl-object-lifecycle-diff.ps1` compares host/client lifecycle logs by sampled frame and aggregates actor-count differences by `objectID/settings`. It supports allowlisted local-only actors and a fail mode.
  - The split wrapper exposes `-RequireNoUnexpectedWorldLifecycleDiff`. With lifecycle tracing enabled, it allows local-only `StageFX(0x012)` and the classified `0x0F0 settings=0x01080002` transient, then fails on any other actor-count difference, including a single sampled row.
  - `logs/codex-pland-processlist-lifecycle-stage1-2400-20260602`: a 10-frame lifecycle trace window on generated course `1` passed Item spawn and all world/global gates. Active avg was `17.635/17.640ms`, max `45.892/42.345ms`, and max consecutive slow frames `1/1`. Same-frame `objectID/settings` comparison found no new persistent world gap after Item repair: remaining differences were the local-role `0x012` settings variant and one sampled row of the already-known host-only `0x0F0 settings=0x01080002` transient.
  - `logs/codex-pland-processlist-lifecycle-result-restart-6200-20260602`: a 30-frame lifecycle trace across result/restart passed the second-game, Big Star, moving-hazard, and 34-field manager/global gates. Active avg was `17.280/17.279ms`, max `49.521/47.596ms`, and max consecutive slow frames `1/2`. Aggregated differences were only local-role `StageFX(0x012)` variants: the regular `settings=0x00008000/0x00008010` split and a host-only `settings=0x00000005` effect while Luigi was dead before result transition. ROM resource symbols map logged vtable `0x02127840` to `_ZTV7StageFX`, so these visual actors stay local.
  - `logs/codex-pland-processlist-lifecycle-threegame-12000-20260602`: a 60-frame lifecycle trace passed three MvL games, two result/restart boundaries, Big Star drift `0/0`, moving-hazard max drift `4096/0` with no sustained row, and the 34-field manager/global gate across `371` rows. Active avg was `17.424/17.424ms`, max `48.162/49.904ms`, and max consecutive slow frames `1/2`. Aggregated differences were still only `StageFX(0x012)`: the local-role split and winner/loser result effects `settings=0x00000005/0x00000016` sharing vtable `0x02127840`.
  - `logs/codex-pland-processlist-lifecycle-luigi-star-3200-20260602`: a 30-frame lifecycle trace passed deterministic real Luigi Big Star pickup, Big Star drift `0/0`, moving-hazard max drift `2048/0` with no sustained row, and the 34-field manager/global gate across `77` rows. Active avg was `17.214/17.216ms`, max `52.192/51.038ms`, and max consecutive slow frames `2/1`. The only aggregated differences were the regular local-role `StageFX(0x012 settings=0x00008000/0x00008010)` pair.
  - `logs/codex-pland-processlist-lifecycle-luigi-death-3720-20260602`: a 30-frame lifecycle trace passed Luigi death, pipe-respawn visibility, moving-hazard progress, Big Star drift `0/0`, and the 34-field manager/global gate across `95` rows. Moving-hazard max drift briefly reached `2048/8192` but had no sustained row. The Y drift was one sampled row at frame `2760` on the same host/client GUID `0x1B`, then converged, so it was not a GUID-map mismatch. Active avg was `17.302/17.303ms`, max `46.113/56.172ms`, and max consecutive slow frames `1/1`. The only aggregated differences were the regular local-role `StageFX(0x012 settings=0x00008000/0x00008010)` pair.
  - `logs/codex-pland-processlist-lifecycle-stage0-2400-20260602` and stages `2-4`: a 30-frame lifecycle matrix passed player movement, Big Star, hazard, and manager/global gates. Stages `2-4` exposed no non-`StageFX` actor-count differences. Course `0` exposed a single sampled row where client had two active `Item(0x01F settings=0x00080002)` actors while host had one, leading to the natural-spawn grace fix below.
- Dense lifecycle diagnosis found a concrete host-only world actor gap on generated course `1` stress:
  - `logs/codex-pland-objectlifecycle-pos-stage1-stress-2120-20260602`: host creates active `Item` actor `0x01F settings=0x00080002` around frame `2046` at fixed-point world position `X=0x00048000`, with associated `0x0F0 settings=0x01080002`; client creates neither.
  - `logs/codex-stocktouch-stress-objectlifecycle-pos-2120-20260602`: forcing both stock inventories to `2` and adding a client lower-screen stock touch still leaves the same `0x01F/0x0F0` creation only on host. This rejects the lower-screen stock-animation interpretation and classifies the gap as a host-side world interaction.
  - ROM symbols provide `Actor::spawnActor` at `0x020A0B64`, and the historical implementation used a narrow `Item` event packet plus client spawn, not a generic actor clone. This branch was removed on 2026-07-15.
  - Same-frame lifecycle re-aggregation found no additional persistent active world gap. The remaining associated `0x0F0 settings=0x01080002` actor is active for about `14` frames before becoming dead, while the `Item(0x01F)` remains active for about `30` frames. Keep `0x0F0` unsynchronized unless a concrete visible or gameplay mismatch appears.
- Historical item-specific lightweight world replication (removed 2026-07-15):
  - The host sent the newest active `Item(0x01F settings=0x00080002)` from the process list. The client called `Actor::spawnActor` once through the ARM trampoline when that host item was missing locally, then applied the host transform while the item remained active.
  - Normal runtime did not enable the diagnostic lifecycle trace. The split wrapper exposed `-WorldStateSpawnItem` and the lightweight `-RequireWorldItemSpawn` gate.
  - `logs/codex-pland-itemspawn-gate-stage1-normal-2400-20260602`: course `1` reproduced and repaired the gap; client spawn/active gates, player movement, Big Star, hazard, and 34-field manager/global gates passed. Active avg `17.229/17.227ms`, max `47.205/38.108ms`, max consecutive slow frames `1/1`, rollback restore/resim `0/0`.
  - `logs/codex-pland-hazard-guidmap-itemspawn-stage1-2400-20260602`: the same generated course `1` route still performed exactly one compensating client spawn and active confirmation after the moving-hazard GUID-map change. Coin/global and world gates passed; active avg `16.949/16.946ms`, max `41.249/41.352ms`, and max consecutive slow frames `1/1`.
  - `logs/codex-pland-itemspawn-luigi-star-settle-fixed-3200-20260602`: real Luigi Big Star acquisition still passed with active avg `16.887/16.887ms`, max `48.209/49.942ms`.
  - `logs/codex-pland-itemspawn-luigi-death-3720-20260602`: death/respawn and pipe visibility still passed with active avg `17.036/17.035ms`, max `43.122/44.764ms`. The hazard had one replacement-boundary sampled row over the drift threshold, then converged.
  - `logs/codex-pland-itemspawn-result-restart-rerun2-6200-20260602`: result/restart into the second game still passed. An earlier identical attempt detected an existing paired-process stall before any Item spawn, with rollback restore/resim still `0/0`; the immediate rerun passed.
  - `logs/codex-manual-pland-itemspawn-default-launch-1800-20260602`: the manual `-PlanDActorSnapshot` launcher propagated `worldStateSpawnItem=1` to both peers and completed with active avg `17.367/17.366ms`, max `31.630/32.403ms`, and `over33ms=0/0`.
  - A generated course `0` route exposed a duplicate-event edge case: both peers created the Item naturally, then the client could spawn the same host GUID again after its local copy disappeared slightly earlier. The historical active-confirmation fix marked that remote GUID as already handled.
  - Post-dedup stage matrix passed for generated courses `0` through `4`: `logs/codex-pland-itemspawn-stage0-dedup-generated-stress-2400-20260602` through `logs/codex-pland-itemspawn-stage4-dedup-generated-stress-2400-20260602`. Course `0` observed the naturally created Item without a client spawn, course `1` performed exactly one compensating spawn plus active confirmation, and courses `2-4` emitted no Item event. Active averages ranged `17.079-17.839ms`; maxima ranged `37.185-49.650ms`; max consecutive slow frames stayed `1`.
  - A later 30-frame lifecycle trace caught a narrower course `0` overlap window: client natural Item GUID `38` was not yet discoverable when the host packet arrived, so immediate compensation spawned GUID `39`; both remained active in the frame `1590` sample. The historical implementation then added a 4-frame natural-spawn grace period before compensation.
  - Post-grace validation passed: `logs/codex-pland-item-grace-stage0-2400-20260602` confirmed the natural client Item as `remoteGuid=38 localGuid=38` without compensation or lifecycle-count drift. `logs/codex-pland-item-grace-stage1-2400-20260602` still performed exactly one required compensating spawn at frame `2051`, confirmed it active at `2052`, and had no non-`StageFX` lifecycle-count drift.
  - Automated lifecycle gate validation passed: `logs/codex-pland-lifecycle-gate-stage1-2400-20260602` performed exactly one required course `1` compensation and reported no unexpected actor-count difference. Running the same gate against pre-fix `logs/codex-pland-processlist-lifecycle-stage0-2400-20260602` fails on `01F/00080002 host=1 client=2` at frame `1590`, so the gate catches the repaired overlap.
  - These logs remain useful historical evidence, but the branch was never used by the GUI/current regression tiers and Plan-D was not a correctness route. The env/config, three item wire slots, Reader/Writer apply state, ARM spawn trampoline, manual propagation, and item-only gate were therefore removed together.
- `scripts/analyze-nsmb-mvl-rollback-log.ps1` now also classifies single-frame or short-run spikes over `MaxSingleFrameMs` as `perf-fail`, so a run like the old 353ms death/pipe case cannot be reported as `ok` just because the average FPS is acceptable. It also avoids marking a completed result-scene trace as a freeze solely because player actors are stationary during the result transition.
- Added `MELONDS_NSML_PERF_SPIKE_PHASE_TRACE=1`. On a slow frame, `NSMB PerfPhaseSpike` reports `mpMs`, `inputMs`, `beforeHookMs`, `runFrameMs`, `afterHookMs`, `drawMs`, `audioMs`, `limitMs`, and `unaccountedMs`. Strict split-input smoke runs and manual `-PlanDActorSnapshot` enable it by default.
  - `scripts/analyze-nsmb-mvl-rollback-log.ps1` summarizes the largest gameplay phase spike from frame `900` onward and its dominant phase, ignoring startup peer-wait noise.
  - `logs/codex-pland-perfphase-v2-stage0-1800-20260602`: strict 1800F route passed with active avg `17.222/17.223ms`, max `44.937/43.706ms`, max consecutive slow frames `1/1`; analyzer selected gameplay phase spikes dominated by `runFrame`.
  - `logs/codex-manual-pland-perfphase-launch-1800-20260602`: manual Plan-D propagation and overhead check passed with `perfSpikePhaseTrace=1`, active avg `17.450/17.448ms`, max `32.223/32.240ms`, and `over33ms=0/0`. Compared with the previous equivalent manual launch at `17.367/17.366ms`, phase timing adds about `0.08ms`.
- Reduced performance-observer interference in strict automation:
  - `ReadGameStateSample()` used to repeat full Main RAM actor scans for each queried actor. An intermediate cache removed the repeated scans, but still swept Main RAM once per diagnostic sample. The diagnostic cache now walks the ROM-analyzed NSMB process execute/delete/render/create and ID lookup lists, deduplicates actor bases, and reuses the resulting live-actor cache for player, star, stage, MvL object, and lifecycle queries. The cache is active only while producing the diagnostic CSV; the normal Plan-D actor/world snapshot path is unchanged.
  - Added `NSMB BeforeHookPhaseSpike` and `NSMB RemoteInputWaitSpike` for strict runs. They split setup, actor-state application, barrier, checkpoint, packet-bridge scratch, outbound network, gate, and final remote-wait costs, and log the remote target frame and wait-loop count.
  - Expanded `NSMB AfterHookPhaseSpike` to split heartbeat, barrier, bridge, lifecycle trace, rollback trace, runtime-force, artifact trace, actor apply, game-state trace, and actor sync costs.
  - `InitFromEnvironment()` now has an atomic initialized fast path, and FPS accounting uses a dedicated mutex instead of contending with the network-pump mutex every frame.
  - Performance-spike trace lines are buffered instead of flushing stdout on the emulation thread. Stall liveness is observed separately through the dedicated heartbeat file.
  - Stall detection now publishes the latest frame to an atomic value every `120` frames by default. A background heartbeat writer thread writes and flushes the tiny dedicated file, so filesystem flush latency no longer pauses emulation.
  - Rejected intermediate measurements exposed observer costs correctly. `logs/codex-pland-beforehook-breakdown-3600-20260602` showed stdout-flush feedback reaching `833.280/1384.609ms`. After buffering the spike traces, `logs/codex-pland-buffered-spiketrace-existingrom-result-restart-6200-20260602` isolated a diagnostic `TraceGameState=789.044ms` Main RAM scan. After switching to the process-list walker, `logs/codex-pland-afterhook-presnapshot-breakdown-4200-20260602` isolated an `heartbeatMs=82.432` synchronous file flush. The async writer removed that emulation-thread cost.
  - Final repeated result/restart validation passed: `logs/codex-pland-async-heartbeat-result-restart-6200-20260602` reached the second game, kept Big Star drift `0/0`, moving-hazard max drift `2048/0`, and passed the 34-field manager/global gate across `177` rows. Active avg was `17.510/17.510ms`, max `56.524/60.689ms`, and max consecutive slow frames `1/1`. Runtime Plan-D `actorStateMs` remained approximately `0.01-0.1ms`; the remaining isolated spikes were packet-bridge peer waits, emulator `runFrame`, or unaccounted scheduling-like time outside the instrumented phases.
  - The longer three-game route also passed with the async observer path: `logs/codex-pland-async-heartbeat-threegame-wins3-12000-20260602` entered game 3 at frame `9961`, kept Big Star drift `0/0`, moving-hazard max drift `4096/0`, and passed the manager/global gate across `371` rows. Active avg was `17.148/17.148ms`, max `52.201/50.323ms`, and max consecutive slow frames `1/1`. Runtime `actorStateMs` stayed approximately `0.01-0.12ms`.
- Added role-specific split inputs for repeated result/restart stress:
  - `tests/nsmb_us_direct_mvl_repeat_result_stress_host.inputs`
  - `tests/nsmb_us_direct_mvl_repeat_result_stress_client.inputs`
- Added role-specific split inputs for the Luigi death/pipe-respawn stress:
  - `tests/nsmb_us_direct_mvl_luigi_death_mario_continues_host.inputs`
  - `tests/nsmb_us_direct_mvl_luigi_death_mario_continues_client.inputs`
- Added role-specific split inputs for the deterministic Luigi Big Star pickup:
  - `tests/nsmb_us_direct_mvl_luigi_star_right_host.inputs`
  - `tests/nsmb_us_direct_mvl_luigi_star_right_client.inputs`
- Configured-ROM generation now normalizes direct-route `fixed` mode to generator `random` while preserving the explicitly selected stage. `logs/codex-pland-singlescan-filtered-stage1-defaultfixed-2400-20260602` verified the default wrapper path, one Item compensating spawn, Big Star/hazard/manager gates, and `courseMode=fixed generatorCourseMode=random`.
- Star/result-continuation route is not a useful actor-snapshot correctness failure yet: `logs/codex-playerstate-cache-star-result-continue-9000-20260602` reached result/restart and held about `59.6fps`, but `RequireStarPickup` failed because star counters stayed `0/0`. Existing baseline `logs/codex-rollback-baseline-starcollect-6200-skipmove-20260601` shows the same `result ... stars=0/0 collected=0/0`, so this route/check needs cleanup before being used as a blocker for actor snapshot.
- The previous full/core rollback issue is still reproduced in logs: rollback/resim paths can spike into hundreds of ms when many inputs arrive or forced delay causes repeated rollback. The actor snapshot path avoids that mechanism entirely.
- Historical dropped-star/effect experiment (packet correction removed 2026-07-15):
  - `logs/codex-pland-effect-sync-stage0-2400-20260602`: Effect slot tracing found active slots at the fixed Effect table, and enabling `WorldStateApplyEffects` kept the normal stage `0` stress route light enough: host/client active avg about `16.89ms`, max `29.835/34.965ms`, max consecutive slow frames `0/1`.
  - `logs/codex-pland-effect-sync-luigi-death-notrace-3720-20260602`: the Luigi death/star-loss route passed without game-state CSV tracing, avg `16.750/16.751ms`, max `39.082/41.908ms`, max consecutive slow frames `1/1`.
  - Full game-state CSV tracing is currently too intrusive for spike decisions on this route: `logs/codex-pland-effect-sync-luigi-death-skipcmp-3720-20260602` hit a `traceMs=180ms` observer spike at frame `2610` even though the no-trace route was light. Use no-trace active timing plus targeted gates/traces for performance decisions.
- Historical stage-object activation / dropped-star refinement (item packet removed 2026-07-15):
  - ROM generation now forces stage-object activation to player `0` before the vsmode stage-lock stubs. This was added after lifecycle traces showed direct local-player differences can prevent an actor from being activated before the runtime snapshot path sees it.
  - Activation validation passed with generated ROMs: `logs/codex-pland-activation-player-stage0-lifecycle-2400-20260602` had `host=52 client=52 shared=52` lifecycle samples, no unexpected actor-count diffs, and active avg `16.788/16.791ms`. The default regenerated stable ROM route also passed: `logs/codex-pland-defaultrom-activation-stage0-2400-20260602`.
  - A short generated stage matrix passed courses `0-4`: `logs/codex-pland-activation-matrix-stage0-1800-20260602` through `logs/codex-pland-activation-matrix-stage4-1800-20260602`.
  - `logs/codex-pland-activation-luigi-death-notrace-3720-20260602` passed the death route after the ROM activation patch, active avg `16.699/16.700ms`, max `37.745/37.963ms`, max consecutive slow frames `1/1`.
  - The historical dropped-star item sync tracked `Item(0x01F settings=0x00090002)` separately from the normal world item and accepted item actor Type `2`; the previous item finder only accepted Type `1`, so short-lived dropped-star items appeared in lifecycle logs but were often missed by the packet sampler.
  - The historical `WireWorldState` was sent both directions, with host-side application limited to `DroppedStarItem`; these item slots and the bidirectional item path were removed on 2026-07-15. Its later 120-byte host-authoritative Big Star form was also removed after the natural-sync audit.
  - `logs/codex-pland-droppeditem-type2-bidir-stage0-trace-2100-20260602` confirmed the dropped-star slot is applied around frame `1876` while staying light: active avg `16.702/16.702ms`, max `26.964/28.009ms`, `over33ms=0/0`.
  - `logs/codex-pland-droppeditem-type2-bidir-luigi-death-notrace-3720-20260602` passed the longer death route with no trace CSV: active avg `16.855/16.855ms`, max `43.114/40.792ms`, max consecutive slow frames `1/1`.
  - `logs/codex-pland-type2-bidir-stage0-lifecycle-only-2400-20260602` passed the lifecycle gate after the Type `2` change: `host=52 client=52 shared=52`, no unexpected actor-count diffs, active avg `16.814/16.815ms`, and `over33ms=0/0`.
- Current enemy/stomp diagnosis:
  - Diagnostic actor-internal tracing now prints object words through `0x1FC` instead of stopping at `0x10C`, so enemy-specific state after the common actor base can be inspected without adding normal runtime cost.
  - `logs/codex-pland-enemy-lifecycle-internals-stage0-2200-20260602` ran a 5-frame lifecycle/internal trace for the existing move/jump/dash stage `0` stress route. It did not reproduce a host/client enemy-count mismatch: the only aggregated lifecycle differences remained local-role `StageFX(0x012)`.
  - `logs/codex-pland-enemy-internals-wide-stage0-1250-20260602` focused on the `0x0053` actor around frame `1155`, where the total actor count drops from `13` to `11` on both peers. The `0x0053` actor and the extended `0x110-0x1FC` region stayed aligned enough that this route is not a useful stomp-desync reproducer. A dedicated enemy-stomp route is still needed.
- Historical moving-hazard refinement (implementation removed 2026-07-15):
  - Applying the already-sent `StateType` and `Flags` fields to matched moving hazards passed `logs/codex-pland-hazard-stateflags-stage0-2400-20260602`: Big Star drift `0/0`, moving-hazard max drift `2048/0`, active avg `16.916/16.914ms`, max `28.863/31.990ms`, `over33ms=0/0`.
  - `logs/codex-pland-stateflags-effect-luigi-death-notrace-rerun-3720-20260602` passed the death route with Effect sync and hazard state/flags apply: avg `17.053/17.052ms`, max `42.598/43.390ms`, max consecutive slow frames `1/1`.
  - Rejected experiment: host-authoritative deactivation of extra local moving hazards made replacement-boundary matching worse. `logs/codex-pland-hazard-deactivate-stage0-2400-20260602` failed at frame `1530` with moving-hazard X drift `317440`, so extra local actors must not be blindly killed during normal lifecycle churn.

Current blocker / caveat:

- Historical manual feedback showed why Plan-D was not a production correctness route: Mario/Luigi contact could freeze the session; broken-block state, minimap markers, and 8-coin item rewards could differ. Those reports span collision/event processing, destructible stage state, UI-facing derived/global state, and item/RNG event selection rather than one actor class. The correction path has since been removed.
- Do not reintroduce expanding per-object transform snapshots or per-symptom host-authoritative events as the primary architecture. Keep the surviving observation traces and targeted gates, and derive recovery work from a systematic rollback checkpoint or a proved ROM-level deterministic fix.
- Latest automated complex-input matrix no longer reproduces active-object drift, moving-hazard drift, or sustained player drift with input delay `0`; however, user manual feedback still reported complex-play desyncs before this fresh-only after-hook fix. Treat freezes/desyncs as improved but not closed until a new manual log or a stronger automatic route covers enemy contact/stomp, star loss, fall death, and continued chaotic input.
- The removed generic actor snapshot was transform-only on apply and did not spawn arbitrary missing actors or synchronize object-specific private state. Active-ID heartbeat and lifecycle gates remain available as observation tools without that correction packet.
- Strict full game-state comparison is still too strict as a promotion gate because it assumes deterministic same-frame equality across many unrelated fields. The current useful gates are targeted: player drift, Big Star, moving hazard, active-ID object diff, lifecycle diff, gameplay heartbeat, and FPS spike/consecutive-slow-frame checks.
- The 12000F three-game route no longer freezes under automated result/restart stress, but ordinary non-rollback frame spikes still exist (`50-52ms` max in that run). Manual play remains required before promotion.
- Added `tests/nsmb_us_direct_mvl_repeat_result_stress.inputs` and exposed `-MvlWins` in the split wrapper. A 12000F repeated death/result/checkpoint-restart route reached MvL stage entries at frames `870`, `5790`, and `9990`.
- Earlier strict runs exposed occasional paired-process stalls in the `278-825ms` range across three-game, Big Star, coin-sync, and item-sync routes. At least the newly reproduced large stalls were observer interference rather than Plan-D snapshot cost: stdout trace flush, diagnostic full-Main-RAM actor scans, and synchronous heartbeat file flush were each isolated and removed from the emulation thread. Keep the older logs as historical caveats because they predate the finer phase traces, but use the current async-heartbeat route for new performance decisions.
- The historical moving-hazard correction used a compact multi-instance snapshot with persistent GUID mapping and nearest-position fallback. It was removed after natural synchronization passed the same targeted gate without correction.
- The historical effect snapshot synchronized only fixed active Effect slots and did not clear local-only effects. It was removed with the unused Plan-D correction branches; the local diagnostic effect trace remains. Other host/client-only actors still require case-by-case ROM/memory classification rather than generic actor cloning.
- Course `1` host-only `Item` creation was historically covered by an item-specific client spawn, but that correction was not part of the GUI/current regression route and was removed on 2026-07-15. The associated `0x0F0 settings=0x01080002` transient also remains unsynchronized. Blindly spawning every host-only actor would replicate local-only effects and is not the intended architecture.
- Result/restart lifecycle traces show local-role `StageFX(0x012)` differences, including a host-only lose/result lead-in effect. Keep these local unless a concrete visible defect appears; do not clone them as gameplay world actors.
- The selected MvL manager/global/stage-scene fields stayed equal during real star acquisition and repeated result/restart. Do not add blind runtime writes for them unless the new observation gate finds a concrete divergent route.
- At this historical checkpoint, the actor/global snapshot path had become a more practical Plan-D-like route for "does not freeze / does not rollback-spike / remote actor moves / pipe death visibility survives", but it was still not a correctness replacement for deterministic rollback. The 2026-07-15 refactor is auditing the remaining branches individually rather than treating this section as a current recommendation.

Next actions:

- Build a Main RAM dirty-page/preimage checkpoint ring and pair it with the smallest fixed core snapshot that still restores/resims correctly. Start with automatic page comparison if write tracking misses JIT/direct writes, then optimize the proven page set and write paths. Measure checkpoint bytes, save/restore p50/p95/max, resim burst cost, active FPS, and consecutive slow frames.
- Use the existing working `coredelta`/full snapshot route as an oracle: after restore/resim, compare hashes and targeted state against the baseline under chaos, contact, death, block-break, and item-reward routes. Add memory domains by automatic mismatch classification rather than by visible symptom.
- Add a low-overhead manual-diagnosis trace mode for mismatches that remain after page-delta coverage. Keep event records in bounded in-memory rings and flush asynchronously so tracing does not create FPS failures. Capture player contact/collision callbacks, actor spawn/destroy events, block/tile writes, Big Star world/minimap setter values, 8-coin reward actor/settings, RNG value/call-count/branch provenance, and the existing heartbeat/perf phase data.
- Use that trace mode with a manual reproduction pass only where the systematic rollback candidate still diverges. Treat the user's visible symptoms as markers, then correlate the first divergent event rather than patching only the visible object that ended up wrong.
- In parallel, build focused automatic routes for player-player contact and block break/item reward. The goal is to make the first collision/event/RNG divergence reproducible without requiring full CSV tracing.
- Use the new heartbeat `activeIds=` output on the next manual or long automated desync to identify concrete host-only/client-only ObjectID/Settings pairs. Prioritize recurring non-`StageFX` / non-`0x10B` differences and decide whether each needs a ROM activation change, a spawn/confirm event, or a narrow object-specific field sync.
- Investigate any recurring `053:00000000` host-only/client-only moving-hazard lifecycle gap using the new heartbeat `hazards=` GUID/position details. Avoid the already rejected "kill extra local hazard" approach; prefer classifying why a peer lacks the hazard at the activation boundary and whether a narrow stage-object activation or host-authoritative spawn/confirm path is justified.
- Extend the chaos automation beyond the current stage0-4 movement matrix to include fall death, item/star loss, enemy contact/stomp, and continued inputs after respawn/result. It should keep using frame heartbeat, gameplay heartbeat, active-ID object diff, targeted world/player drift gates, and FPS-spike gates rather than full CSV equality.
- Keep the three-game stress route in repeated performance sweeps so occasional paired-process stalls remain visible instead of being hidden by average FPS.
- Keep the phase traces and async dedicated-heartbeat stall detector enabled in automation, but do not use full game-state CSV traces as the primary FPS-spike signal on long routes; they can create observer spikes. Prefer no-trace active timing plus targeted world/effect/lifecycle traces.
- Continue ROM/memory analysis for enemy stomp correctness. The unsafe "deactivate extra local hazards" path is rejected; after the activation-player patch, the next useful direction is building a deterministic enemy-stomp reproducer, then identifying narrow enemy damage/death/stomp state fields or a proper StageEntity/Actor destroy event rather than blindly killing extras.
- Use the MvL manager/global observation gate on new routes and add only fields that show a concrete persistent mismatch, without falling back to full savestate or full CPU rollback.
- Reuse `scripts/analyze-nsmb-mvl-object-lifecycle-diff.ps1` on new lifecycle traces and investigate only persistent non-`StageFX` gaps.
- Tighten drift thresholds after more route coverage; the current sustained-drift gate is meant to catch gross desync/freeze without rejecting a transient one-row correction.

## 2026-06-02 current status - real rollback gate and Plan-D-like retest

Current working candidate is still experimental:

- `nsmbtinycore + delta-discovered globals + process-list object ranges + actorArena + ARM9 stack + no heap scan + tinyCoreFlags=0x241`.
- Manual explicit `-RollbackBackend nsmbtinycore` under `-LowLatencyRollback` now defaults to checkpoint interval 1, input max frame lead 1, `RollbackInputWaitUs=2500`, `RollbackMaxResimFrames=1`, network pump 50us, JIT reset skip, render skip during resim, and CP15 PU debug suppression.
- `coredelta` remains the correctness/perf baseline. The lightweight path is still not promoted.

Validation changes:

- `scripts/run-nsmb-mvl-split-local-input-smoke.ps1` now has `-MinRollbackResims`, so a bounded-input-wait run cannot pass as a rollback test when it avoided rollback entirely.
- `scripts/run-nsmb-mvl-rollback-candidate-sweep.ps1` passes the minimum-resim gate, scans candidate logs recursively, records `rollbackResims` summary lines, exposes `-RollbackMaxResimFrames`, and can force remote input delay with `-InputSendDelayFrames`.
- `nsmbranges-proclist-arena-gpu2d-noheap` was added as a RAM-only Plan-D extreme candidate; it is useful as a negative control.

Implementation changes:

- NSMB range restore no longer invalidates all Main RAM JIT pages. It invalidates only restored ranges, and honors `MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET=1`.
- CP15 PU-region debug logging can be suppressed with `MELONDS_NSML_SUPPRESS_PU_DEBUG=1`, removing large rollback-time stdout bursts.

Latest measurements:

- The old manual comparison still matches the user report: `logs/nsmb-mvl-manual-local-20260601-212956` is `nsmbtinycore` abort/low-FPS failure, while `logs/nsmb-mvl-manual-local-20260601-213213` is a non-frozen `coredelta` baseline.
- `RollbackInputWaitUs=8000 + netpump 50us` is no longer treated as a valid rollback pass by itself. `logs/codex-sweep-tinycore-rbwait8000-minresim1-1600-20260602/20260601-234507` fails correctly with `resims=0`.
- Current best real-rollback natural route: `logs/codex-sweep-tinycore-suppresspu-natural-wait2500-compare-2400-20260602/20260602-000856`. It passed state comparison but still failed strict rollback spike gate by a small margin: host max `32.935ms`, client rollback spike `33.894ms`.
- Relaxed correctness proof for the same settings passed 2400F: `logs/codex-sweep-tinycore-maxresim1-wait2500-netpump-correctness-2400-20260602/20260601-235854`.
- Forced one-frame send delay remains unacceptable: `logs/codex-sweep-tinycore-suppresspu-forced-delay1-wait2500-compare-2400-20260602/20260602-000614` saw repeated rollback frames, active max `102.108ms`, and `over33ms=505`.
- RAM-only `nsmbranges` is rejected: `logs/codex-sweep-nsmbranges-forced-delay1-wait2500-compare-1600-20260602/20260602-000804` data-aborted around frame 961 despite very low restore cost.

Current conclusion:

- The lightweight snapshot size is not the main blocker anymore. CPU/core restore plus full-frame `nds->RunFrame()` resim is the remaining cost.
- CPU core restore is required for correctness; RAM-only actor/global restore is too unstable.
- The best tinycore path is close under natural localhost timing but not robust under forced delay. Next useful direction is a narrower CPU/timer/core subset or a game-level actor/global apply that avoids full NDS resim, not wider Main RAM snapshots.

## 2026-06-01 prior status - spike gate and Plan-D-like experiments

Current working candidate is still experimental:

- `nsmbtinycore + delta-discovered globals + process-list object ranges + actorArena + ARM9 stack + no heap scan`.
- Best current no-freeze/no-rollback-spike automation setting adds bounded same-frame input wait: `MELONDS_NSML_ROLLBACK_INPUT_WAIT_US=8000` plus `MELONDS_NSML_NET_PUMP_THREAD=1` / `MELONDS_NSML_NET_PUMP_SLEEP_US=50`.
- Manual/log comparison confirmed the user's report: `logs/nsmb-mvl-manual-local-20260601-212956` is an `nsmbtinycore` abort/low-FPS failure, while `logs/nsmb-mvl-manual-local-20260601-213213` is a non-frozen `coredelta` baseline.
- The aborting manual log predates the ARM9 stack addition. With the current stack range, `logs/codex-currentframefix-tinycore-long-4200-20260601/20260601-225143/nsmbtinycore-proclist-arena-noheap` passes 4200F without abort/stall, checkpoint size `398,399` bytes, save avg about `0.18ms`, restore avg about `3.6ms`.

Spike-aware validation added:

- `scripts/run-nsmb-mvl-split-local-input-smoke.ps1` now has `-MaxRollbackFrameMs`; it fails specifically when a frame containing `rollbackResimDelta > 0` exceeds the limit, so average FPS can no longer hide rollback stutter.
- `NSMB PerfSpike` now prints `rollbackRestoreDelta` and `rollbackResimDelta`, which lets the harness distinguish rollback spikes from ordinary slow frames.
- `scripts/run-nsmb-mvl-rollback-candidate-sweep.ps1` passes the new rollback-frame gate and classifies it as `perf-fail`. It also has `-MaxActiveFrameMs` so non-rollback frame spikes can be gated separately.

Performance experiments:

- Skipping JIT reset on rollback restore (`MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET=1`) reduced tinycore restore avg from roughly `9.5ms` to roughly `3.5-3.8ms`.
- Skipping render during rollback resim (`MELONDS_NSML_ROLLBACK_RESIM_SKIP_RENDER=1`) did not remove the visible spikes; the remaining cost is mainly the full NDS `RunFrame()` resimulation.
- With checkpoint interval 8 / frame lead 8, strict rollback spike gate still sees about `150-180ms` max rollback frames.
- With checkpoint interval 1 / frame lead 1, the same route improves to about `54-76ms` max rollback frames, but still fails a 33ms no-stutter gate.
- An experimental `MELONDS_NSML_ROLLBACK_MAX_RESIM_FRAMES=1` cap reduced max rollback frames to about `38-40ms` in 1600F, but the 4200F correctness run failed at frame 1110 (`playerActor0Y` mismatch). This is not promotable.
- A Plan-D-like remote-player actor snapshot mode (`MELONDS_NSML_STATE_APPLY_MODE=remote-player`) was added for experiments, but no-rollback state-apply testing ran at only about `38fps` and failed the movement-probe harness. It is not a replacement for rollback yet.
- Bounded same-frame remote input wait is not an input-delay scheme and does not change local frame delay, but it is also not the final Plan-D snapshot answer. It prevents prediction in the common localhost case and therefore avoids full rollback resim spikes.
- `RollbackInputWaitUs=8000` without network pump passed the 4200F rollback-spike gate but still showed occasional non-rollback active-frame spikes around `39ms`.
- `RollbackInputWaitUs=8000` plus network pump 50us passed 4200F move+jump+dash stress with game-state comparison, `-MaxRollbackFrameMs 33`, `-MaxActiveFrameMs 50`, and `-MaxConsecutiveSlowFrames 120`: `logs/codex-sweep-tinycore-rbwait8000-netpump-param-rb33-active50-4200-20260601/20260601-233536`. Active timing was about `avgFrameMs=17.15`, `maxFrameMs=44.51`, active FPS about `58.3`, and `rollbackResims=0` in the sampled active window.

Current conclusion:

- The old "案D寄りが固まる" report was real. The current stack-augmented tinycore candidate no longer reproduces that abort in the 4200F automated route, but rollback resimulation still causes noticeable spikes.
- Lightweight snapshot size is no longer the dominant cost. The blocker is full-frame resimulation: even a 398KB restore is followed by 1-2 full `nds->RunFrame()` calls.
- Manual explicit `-RollbackBackend nsmbtinycore` under `-LowLatencyRollback` now defaults to checkpoint interval 1, input max frame lead 1, rollback input wait 8000us, and network pump 50us because that is the best measured tinycore setting so far. It is still experimental, not the final no-stutter answer.
- Next direction: keep `coredelta` as correctness baseline, keep tinycore as the light snapshot candidate, and continue ROM/memory-level work to avoid full NDS resim rather than relying only on bounded same-frame waiting.

## 2026-06-01 prior lightweight direction - actor arena + ARM9 stack snapshot

Current Plan-D-like candidate: `nsmbtinycore + delta-discovered globals + process-list object ranges + actorArena + ARM9 stack + no heap scan`.

Implementation:

- Added `MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES=1`.
- Added `MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE=1`.
- Actor arena range currently adds `0x021B2600+0x5000` and `0x02088B00+0x200`.
- ARM9 stack/scratch range currently adds `0x023E0000+0x20000`. This comes from the failed manual `nsmbtinycore` run where ARM9 aborted with `sp=0x027E38F4`, which mirrors into Main RAM near `0x023E38F4`.
- This is intentionally not a full savestate and not full Main RAM. It is a small static actor/global/stack snapshot plus process-list-derived live objects and tiny core state.

Manual-log classification:

- `logs/nsmb-mvl-manual-local-20260601-212956`: user-reported frozen Plan-D-like run. New analyzer classifies it as `abort`; client has `ARM9: prefetch abort (frame=2077 pc=00000004 ...)`, host `maxConsecutiveSlowFrames=409`, client `maxConsecutiveSlowFrames=251`.
- `logs/nsmb-mvl-manual-local-20260601-213213`: user-reported non-frozen baseline run. Analyzer classifies it as `ok`; host/client `maxConsecutiveSlowFrames=4/4`.
- Added `scripts/analyze-nsmb-mvl-rollback-log.ps1` so forced-close manual logs can be classified after the fact instead of being dismissed as only `missing frame limit`.

Verification:

- `logs/codex-nsmbcoreranges-proclist-arena-noheap-compare-6000-20260601`: same NSMB Main RAM ranges with full non-MainRAM core state passed 6000F. This suggests the old failure was not just actor range coverage; missing rollback state around stack/core interaction was plausible.
- `logs/codex-tinycore-flag-probe-pred1-2400-20260601` and `logs/codex-tinycore-fullflag-probe-pred1-2400-20260601`: very aggressive prediction-probe-every-frame stress fails even for `coredelta`, so it is retained as an overload diagnostic, not a promotion gate.
- `logs/codex-sweep-pred10-limit100-3600-20260601`: moderate prediction probe passed both `coredelta` and pre-stack actorArena candidate. `logs/codex-sweep-pred5-limit200-3600-20260601` and `logs/codex-sweep-pred8-limit120-3600-20260601` fail even baseline, so they are too severe for current correctness gating.
- `logs/codex-sweep-tinycore-arena-stack-6000-20260601`: stack-augmented candidate passed 6000F move+jump+dash stress with slow-run gate.
- `logs/codex-sweep-tinycore-arena-stack-trace-2600-20260601`: stack-augmented candidate passed with trace. Checkpoint `bytesLast=398,399`, `saveAvgUs=192`, `restoreAvgUs=9,519`, `tinyFlags=0x200`, `actorArena=1`, `arm9Stack=1`, `heapScan=0`.
- `logs/codex-sweep-tinycore-arena-stack-pred10-limit100-3600-20260601`: stack-augmented candidate passed moderate prediction-probe stress.
- `logs/codex-sweep-tinycore-arena-stack-death-skipprobe-4200-20260601`: stack-augmented candidate passed the Luigi death/Mario continues route with game-state comparison.

Current conclusion:

- The old 270KB actorArena/noHeap candidate is not trusted for manual play because the user reproduced a freeze and the log shows ARM9 abort plus long slow-run.
- The current candidate is about 398KB, still much lighter than the 2.5MB `coredelta` baseline, and now includes the stack range implicated by the abort. It is an experimental manual candidate only, not final.
- Manual explicit `-RollbackBackend nsmbtinycore` under `-LowLatencyRollback` now uses actorArena/processList/ARM9-stack/noHeap by default, with checkpoint interval 1 and input max frame lead 1 from the later spike tests.

## 2026-06-01 prior automation state - slow-run detection

User clarification: the star pickup/fall-death freeze was only an example from an automated run. The real bug was that automation treated runs as passed even when melonDS had become effectively stuck at very low FPS. The harness must detect both hard frame-progress stalls and long consecutive slow-frame runs.

Implemented detection:

- `scripts/run-nsmb-mvl-lan-route-smoke.ps1`: keeps the frame-progress watchdog through `-StallTimeoutMs`, `-StallStartFrame`, `-StallPollMs`, and stdout heartbeat parsing.
- `scripts/run-nsmb-mvl-split-local-input-smoke.ps1`: now supports `-SlowFrameThresholdMs` and `-MaxConsecutiveSlowFrames`. It parses `NSMB PerfSpike` frame numbers and fails if frames over the threshold are consecutive for too long.
- `scripts/run-nsmb-mvl-split-local-input-smoke.ps1`: enabling `-MaxConsecutiveSlowFrames` now forces `MELONDS_NSML_FPS_SPIKE_TRACE=1` and ensures the spike threshold is not higher than the slow-frame threshold, so the check cannot silently no-op.
- `scripts/run-nsmb-mvl-rollback-candidate-sweep.ps1`: candidate sweep now passes the same consecutive-slow-frame gate and classifies it as `perf-fail`.
- `scripts/run-nsmb-mvl-manual-local.ps1`: `-LowLatencyRollback` keeps heartbeat logging and now enables game-state/life/defeated traces by default so forced-close manual failures leave more useful logs.

Latest verification:

- PowerShell parse passed for the touched smoke/manual scripts.
- The previous `nsmbtinycore` 6000F result is no longer valid as a pass. Re-run with `-MaxConsecutiveSlowFrames 120 -SlowFrameThresholdMs 33` failed correctly: `logs/codex-detect-slowrun-nsmbtinycore-dual-stress-6000-20260601`, host `maxConsecutiveOver33=2519`, client `maxConsecutiveOver33=708`, active FPS around 19.
- `nsmbtinycore` with `RollbackCheckpointInterval=16` still failed: `logs/codex-detect-slowrun-nsmbtinycore-ckpt16-dual-stress-6000-20260601`, host `maxConsecutiveOver33=708`, active FPS around 19. Checkpoint frequency affects the symptom but does not fix it.
- `coredelta-page256-k30` under the same 6000F move+jump+dash stress and the same slow-run gate passed: `logs/codex-detect-slowrun-coredelta-page256-k30-dual-stress-6000-20260601`, host/client `maxConsecutiveOver33=2/3`, active FPS around 51.

Current conclusion:

- The old automation was wrong: average FPS and frame-limit completion were insufficient. Consecutive slow-frame detection is now part of the pass/fail gate.
- `coredelta-page256-k30` remains the practical correctness/perf baseline for zero-delay rollback. It is still heavy, but it does not show the long solid low-FPS failure in the current 6000F stress.
- `nsmbtinycore + delta-discovered + light GPU3D` is not usable as current best despite being much lighter. It can degrade into long low-FPS runs without a hard process stall.
- Next ROM/memory-analysis direction: keep `coredelta` as the baseline, use its page-delta coverage and the NSMB process/object list code to find a smaller actor/global range set, and do not promote a lightweight backend unless it passes the consecutive-slow-frame gate.

## 2026-06-01 superseded automation note - stall watchdog and old sweep

User requirement clarified: the next rollback work must not rely on manual observation of "melonDS froze". The test harness now has a frame-progress watchdog. When `-StallTimeoutMs` is set, melonDS emits `NSMB Heartbeat: inst=... frame=...` every 30 active frames, and the wrapper kills the child process if the latest observed frame stops advancing after `-StallStartFrame`. This catches the manual-style hard freeze even when the user has to force-close melonDS and the normal end-of-run logs are incomplete.

Implemented scripts:

- `scripts/run-nsmb-mvl-lan-route-smoke.ps1`: added `-StallTimeoutMs`, `-StallStartFrame`, `-StallPollMs`, heartbeat env setup, and child-process stall detection from stdout progress lines.
- `scripts/run-nsmb-mvl-split-local-input-smoke.ps1`: passes stall watchdog parameters through to host/client runs.
- `scripts/run-nsmb-mvl-manual-local.ps1`: `-LowLatencyRollback` now defaults to `-StallTimeoutMs 5000`, so manual low-latency runs also leave progress heartbeats for diagnosing freezes.
- `scripts/run-nsmb-mvl-manual-local.ps1`: explicit `-RollbackBackend nsmbtinycore` under `-LowLatencyRollback` now configures `MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES=1`, scan interval 30, and `tinyCoreFlags=0x200` by default for the current lightweight candidate.
- `scripts/run-nsmb-mvl-rollback-candidate-sweep.ps1`: runs rollback backend candidates under the same move+jump+dash stress input and writes `summary.csv` with `passed`, `mismatch`, `stalled`, `abort`, `timeout`, or `perf-fail`.

Latest verification:

- Build passed: `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4`.
- Watchdog normal path passed: `logs/codex-stall-watchdog-coredelta-smoke-1600-20260601`.
- Watchdog intentional trip passed: `logs/codex-stall-watchdog-intentional-trip-20260601` failed with `melonDS process stalled... latestFrame=1020`, confirming the wrapper can detect frame-progress stalls automatically.
- Important fix: `EnvInt` now uses `strtol(..., base 0)`, so hex env values such as `MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS=0x200` are parsed correctly. Earlier tiny-core runs that printed `tinyFlags=0x0` were not valid flag tests.
- Candidate sweep after the hex fix: `logs/nsmb-mvl-rollback-candidate-sweep/20260601-190040/summary.csv`.
- Long validation for the current lightweight candidate: `logs/nsmb-mvl-rollback-candidate-sweep/20260601-190537/summary.csv` and no-trace run `logs/nsmb-mvl-rollback-candidate-sweep/20260601-190741/summary.csv`.

Current candidate results from the post-fix sweep:

- `coredelta-page256-k30`: `passed`. Still heavy: final stats around `bytesLast=2,465,673`, `bytesAvg=2,742,996`, `saveAvgUs=3914`, `restoreAvgUs=22297`; timing `avgFrameMs=20.012`, `maxFrameMs=301.520`, `over25ms=114`, `over33ms=35`.
- `nsmbtinycore-expanded`: `passed` at 2600F with comparison and trace. This is the current best lightweight candidate: `bytesLast=269,175`, `bytesAvg=268,552`, `saveAvgUs=2393`, `restoreAvgUs=12310`, `tinyFlags=0x200`, `deltaDiscovered=1`. Timing was `avgFrameMs=20.979`, `maxFrameMs=316.254`, `over25ms=132`, `over33ms=40`.
- `nsmbtinycore-proclist-heap900`: `mismatch` at `frame=1590 movingHazardX`. ProcessList+low-frequency heap scan lowers save cost (`saveAvgUs=247`) but currently drops a required moving-hazard range.
- `nsmbcoreranges-proclist-heap900`: `mismatch` at `frame=1590 movingHazardX`; around `2.56MB`, `saveAvgUs=3906`, `restoreAvgUs=14599`, and still not correct.

Long validation for `nsmbtinycore-expanded`:

- 6000F comparison+trace passed: `bytesLast=269,175`, `bytesAvg=268,963`, `saveAvgUs=2366`, `restoreAvgUs=11411`, `maxFrameMs=298.646`, `over25ms=281`, `over33ms=34`.
- 6000F no-trace playlike run passed: `avgFrameMs=17.403`, `maxFrameMs=285.308`, `over25ms=123`, `over33ms=20`.

Current conclusion:

- `nsmbtinycore-expanded` is now the most promising rollback backend for the user-requested "light checkpoint /案D寄り" direction. It is roughly 269KB per checkpoint in this stress route, versus roughly 2.46MB for `coredelta`.
- The current blocker has shifted from correctness to spike reduction and broader validation. Even the lightweight candidate still has large single-frame spikes around 285-316ms in these local two-instance stress runs.
- Next search direction: keep `nsmbtinycore-expanded` as the candidate baseline, test longer/manual-like sessions with the watchdog enabled, and then try to replace scan30 heap discovery with a ROM/memory-derived static actor/global range set so `saveAvgUs` and spike counts drop without losing correctness.

## 2026-06-01 current direction - ROM/memory analysis and spike-aware validation

Update after manual play: `nsmbcoreranges` is still not acceptable as the default manual path. User manual run `logs/nsmb-mvl-manual-local-20260601-182701` used `rollbackBackend=nsmbcoreranges` and froze during play after many rollback resimulations and repeated `NSMB PerfSpike` lines around frame 1900-2633. The immediately following user run `logs/nsmb-mvl-manual-local-20260601-182807` used `rollbackBackend=coredelta` and reached result/restart logging around frame 3313 without the same freeze. Therefore `scripts/run-nsmb-mvl-manual-local.ps1 -LowLatencyRollback` default is back to `coredelta`; `nsmbcoreranges` remains an explicit experimental analysis backend only.

方針を `coredelta` 固定ではなく、案D寄りのROM/メモリ解析で正しい軽量snapshotを作る方向へ戻した。`coredelta` は引き続き安全基準として残すが、軽量化候補の検証は `coredelta` の `MELONDS_NSML_ROLLBACK_DELTA_PAGE_TRACE=1` で実際に変化したMain RAM pageを取り、既存NSMB range候補で未カバーのpageを集計して進める。

追加した検証/解析:

- `NSMB Test: active frame timing ... avgFrameMs/maxFrameMs/over16ms/over25ms/over33ms` を終了時に出すようにした。平均FPSだけでは見えないガクッとした落ち込みを検出するため。`MELONDS_NSML_FPS_SPIKE_TRACE=1` と `MELONDS_NSML_FPS_SPIKE_THRESHOLD_MS` で逐次 `NSMB PerfSpike` も出せる。
- `scripts/run-nsmb-mvl-split-local-input-smoke.ps1` に `-MaxActiveFrameMs` / `-MaxActiveFrameOver25ms` / `-MaxActiveFrameOver33ms` を追加し、FPS平均だけでなく瞬間dropをgateできるようにした。
- `scripts/analyze-nsmb-rollback-delta-pages.ps1` を追加。`NSMB RollbackDeltaPagesUncovered` を集計し、未カバーrangeを頻度順に出す。

delta-page解析結果:

- `logs/codex-rollback-delta-page-trace-knownranges-990-2600-20260601`: 既知range入りでも `uncoveredFrames=163`, `ranges=64`。上位は `0x02190400`, `0x021B4F00`, `0x02095600`, `0x0208FA00`, `0x0229BD00`, `0x02350E00` など。
- 上位rangeを `AddNSMBRollbackDeltaDiscoveredRanges` に追加後、`logs/codex-rollback-delta-page-trace-expandedranges-v2-990-2600-20260601` は `summaries=402`, `uncoveredFrames=0`, `ranges=0`。少なくとも990-2600Fの移動+ジャンプ+ダッシュstressでは、既知range候補が `coredelta` 変更pageを覆うところまで来た。

拡張rangeでの `nsmbcoreranges` 再検証:

- `logs/codex-nsmbcoreranges-expandedranges-stress-compare-2600-20260601`: `nsmbcoreranges` / `InputDelayFrames=0` / move+jump+dash stress / game-state比較ありで2600F通過。checkpointは約 `2,559,101` bytes、saveAvgUsは約 `5.57ms`、restoreAvgUsは約 `12.4-12.8ms`、active fpsは約 `53fps`。
- `logs/codex-nsmbcoreranges-expandedranges-stress-playlike-2600-20260601`: trace/game-stateなし寄りで2600F通過。active fpsは host/client `53.09/53.37`、throttleは0。ただしactive frame timingは `maxFrameMs=213-252ms`, `over25ms=84-87` で、ガクッとしたdropは残る。
- 旧候補が停止した条件に近い `InputSendDelayFrames=6` / 6000F stress は `logs/codex-nsmbcoreranges-expandedranges-delay6-6000-20260601` で完走。以前の `arm9PC=0xffff0104` / `arm9SP=0x0` 停止はこのrange拡張では再発していない。active fpsは約 `45.5fps`、restoreOpsは host/client `116/111`、restoreAvgUsは約 `11.5-12.4ms`。重い遅延stressなので通常性能とは分ける。
- 手動向け `-LowLatencyRollback` の既定は `coredelta`。`nsmbcoreranges` は `-RollbackBackend nsmbcoreranges` で明示した解析用に限定する。

Current blocker: 拡張 `nsmbcoreranges` は停止耐性は改善したが、checkpoint sizeが約2.56MBのままで、案Dの軽量actor/global snapshotとはまだ言えない。次は、今回追加したpageのうち本当に必要なactor/global/stack/scratchだけをROM/メモリ構造で分類し、ProcessList/global由来の小さいrangeへ置き換える。

## 2026-06-01 rollback stress update - authoritative current note

現在の手動向け `-LowLatencyRollback` は、`nsmbcoreranges` から `coredelta` へ戻した。理由は、移動 + ジャンプ + ダッシュ同時入力の長時間stressで `nsmbcoreranges` が停止/timeoutし、client側 game-state trace では `arm9PC=0xffff0104` / `arm9SP=0x0` になったため。`nsmbcoreranges` はcore stateを過去へ戻す一方で Main RAM はNSMB推定rangeだけを戻すため、range外のスタック/一時領域/周辺Main RAMが現在フレームのまま残り、resimulate時にCPU状態とRAM状態が噛み合わなくなる可能性が高い。

現行候補は `RollbackBackend=coredelta` / `RollbackWindow=64` / `RollbackCheckpointInterval=8` / `RollbackResimulate` / `InputDelayFrames=0` / `InputMaxFrameLead=8` / `MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL=30` / `MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE=256`。これは「page-delta Main RAM + core state」寄りで、軽量な案D actor/global snapshotではない。

移動 + ジャンプ + ダッシュstressの結果:

- `nsmbcoreranges`, `InputSendDelayFrames=6`, 6000F: wrapperは成功扱いしていたが、実際はhost/client childがtimeout。game-stateは3960Fで止まり、client ARM9は異常PC/SPになった。テストハーネス側も、child stderr timeoutを見逃す経路があったため修正した。
- `coredelta`, page 256, keyframe 30, `InputSendDelayFrames=6`, 6000F: 完走。active fps は host/client とも約 `43.1fps`。重い遅延stressなので通常プレイFPSとは分けて扱う。
- `coredelta`, page 256, keyframe 30, `InputSendDelayFrames=6`, 2600F, game-state comparisonあり: 完走。座標/global hash系の比較は通過。
- `coredelta`, page 256, keyframe 30, no artificial send delay, 2600F, game-state comparisonあり: 完走。active fps は約 `52.1fps`。
- no artificial send delay, trace/game-stateなしのプレイ寄り2600F: active fps は約 `53.5fps`。`InputMaxFrameLead=16` ではthrottle 0件になったがfpsは約 `53.0fps`で、主な残りコストはthrottleではなくcheckpoint保存/resimulate。`RollbackCheckpointInterval=16` は約 `50.6fps`まで悪化したため、現時点ではinterval 8を維持する。

フレーム落ちの主因は、rollback発生フレームで `restore + 過去checkpointから現在フレームまでのRunFrame再実行 + checkpoint再保存` を同じ表示フレーム内で行うこと。現行 `coredelta` は通常delta checkpointでも約 `2.46-2.49MB`、平均約 `2.7MB`、keyframeは約 `6.6MB`。save平均はおおむね `3.5-4.1ms`、restore平均は `17-20ms` 程度まで出る。したがって、さらに軽い actor/global snapshot を正しく作れれば改善余地はある。ただし `nsmbcoreranges` の失敗から、Main RAMを推定rangeだけに削るとCPU stateとの整合性が壊れやすい。軽量化はROM/メモリ解析で「戻すべきゲーム状態」と「戻してはいけないinput/net volatile領域」を確定してから進める。

## 2026-06-01 older manual rollback status - superseded

Superseded by the current automation note above: `-LowLatencyRollback` manual default is now `coredelta`, not `nsmbcoreranges`. The older text below is retained only as historical context for the manual-run debugging path.

手動プレイ用の現行コマンドは `scripts/run-nsmb-mvl-manual-local.ps1 -LowLatencyRollback -AllowJit`。
`-LowLatencyRollback` は `InputDelayFrames=0` / `InputMaxFrameLead=8` / `RollbackBackend=nsmbcoreranges` / `RollbackWindow=64` / `RollbackCheckpointInterval=8` / `RollbackResimulate` / `PacketBridgeStartFrame=870` を設定し、必要な `MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES=1` と `MELONDS_NSML_ROLLBACK_NSMB_SCAN_INTERVAL=30` もスクリプト内で設定する。
同時に `MELONDS_NSML_FIXED_FRAME_SLEEP=1` と `MELONDS_NSML_PERF_BREAKDOWN=1` を有効にし、2プロセス起動時のbusy-yieldによるCPU消費を抑えつつ、手動runでも `NSMB Perf` 行でフレーム進行と処理内訳を確認できるようにした。
手動ログは既定で `logs\nsmb-mvl-manual-local-yyyyMMdd-HHmmss` に分けて保存するように変更したため、失敗回のログが上書きされにくい。

直近の手動失敗ログは `logs\nsmb-mvl-manual-local` に残っていたが、固定ディレクトリのため2回分が上書きされていた。残っていたログでは host/client とも `localFrame=860` の start-ready 受理直後に終了し、wrapper は `missing frame limit` 扱いになっていた。
修正後の有限検証では、`logs/codex-manual-local-lowlatrollback-script-1200-20260601` が1200Fまで通過し host/client active fps は `58.72/58.63`、`logs/codex-manual-local-lowlatrollback-script-2600-20260601` が2600Fまで通過し active fps は `57.95/58.00`、throttle は両側0だった。
追加で `-SoftwareRenderer` を外し、OpenGL compute + fixed sleep + perf breakdown の `logs/codex-manual-lowlat-fixedsleep-opengl-2600-20260601` は2600Fまで通過し、host/client active fps は `59.55/59.65`、throttle は両側0だった。無期限runの `logs/codex-manual-lowlat-fixedsleep-perf-opengl-20260601` でも870以降に `NSMB Perf` が出ており、ログが870で止まるように見えていた主因は通常ログ不足だった。
座標同期の再確認として `logs/codex-split-lowlat-nsmbcoreranges-ckpt8-start870-predprobe-2600-20260601` で prediction probe ありの2600F game-state比較を通過した。active fps はtraceと比較込みで `52.65/52.64`。

Current blocker: 実手動入力でユーザー環境の停止をまだ直接再現できていない。次に停止した場合は、新しいタイムスタンプ付きログディレクトリの `host.stdout.txt` / `client.stdout.txt` / `wrapper/*.err.txt` を読み、`NSMB Perf` が870以降も進んでいるか、`runFrameMs` / `beforeHookMs` / `limitMs` のどれが増えているかで切り分ける。

## 2026-06-01 zero-delay manual candidate

`InputDelayFrames=4` で通った結果は、rollback成立確認として扱わない。`InputDelayFrames=0` / `InputMaxFrameLead=8` で再検証したところ、旧 `nsmbtinycore + TINY_CORE_FLAGS=0x200` 候補は frame 1950 付近で `netPacketTick` がhost側だけ `0x65c -> 0x437` に巻き戻り、座標不一致を起こした。

現時点で手動プレイに出せるゼロ遅延候補は `nsmbcoreranges + delta-discovered ranges + checkpointInterval=8 + rollbackWindow=64`。`logs/codex-delay0-nsmbcoreranges-resim-patches-ckpt8-rendered-lead8-gamestate-2600-20260601` はgame-state比較ありで2600F通過し、host/client active fps は `56.51/56.77`。game-state traceなしの同条件 `logs/codex-delay0-nsmbcoreranges-resim-patches-ckpt8-rendered-lead8-nogamestate-2600-20260601` はhost/client active fps `57.40/57.62`。

正しさの主因は、tiny coreではなく通常core stateを保存する必要があること。`nsmbtinycore` は軽いが、ゼロ遅延rollbackで実際にresimが走るとNSMB packet/global状態が一致しない。`nsmbcoreranges` はcheckpoint sizeが約 `2.54MB` と重い一方、現在のゼロ遅延候補としては座標一致を維持している。

## 2026-06-01 manual play correction

手動プレイ向けに一度案内した `InputDelayFrames=0 / InputMaxFrameLead=8` は未検証で、2600Fの自動入力・game-state比較でframe 1950に `playerActor0X` 不一致を起こした。手動プレイ推奨から外す。

現時点で手動プレイに使う設定は、旧候補rollbackに `InputDelayFrames=4 / InputMaxFrameLead=4 / InputUnreliable / InputBundleHistory=8` を組み合わせる。描画あり・自動入力・game-state比較ありの2600Fは `logs/codex-manual-safer-oldcandidate-rendered-delay4-lead4-gamestate-2600-20260601` で通過し、host/client active fps は `59.54/59.48`。

手動起動パスでも `-SoftwareRenderer` 付きの1800F run `logs/codex-manual-local-software-oldcandidate-delay4-lead4-1800-20260601` がhost/client active fps `57.80/57.56` で完走した。手動時に10fps級まで落ちる場合は、まずOpenGL compute rendererではなく `-SoftwareRenderer` を使う。

## 2026-06-01 ProcessList-centered rollback snapshot

現在の最有力候補は `nsmbtinycore + MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS=0x200 + MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES=1` を維持しつつ、NSMB Code Reference の `ProcessManager` 構造を使って actor/object range を作る方式。

新しい実験フラグ:

- `MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES=1`: `Game::executeProcess/deleteProcess/renderProcess/createProcess/idLookupProcesses` をたどって実在objectをsnapshot対象にする。
- `MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES=1`: 従来のMain RAM object風heap scanをfallbackとして使う。互換性のためdefaultは有効。
- `MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL=900`: fallback heap scanだけを低頻度化する。`MELONDS_NSML_ROLLBACK_NSMB_SCAN_INTERVAL=30` のProcessList更新とは分離した。

結論:

- ProcessList-only (`heapScan=0`) は通常FPSと短距離probeでは良い。1500F traceで `saveAvgUs=157-159us`、1800F no-trace FPSでhost/client `59.93/59.97fps`、2600F game-state + predprobe10 limit6も通過した。
- ただし ProcessList-only は6000Fの後着入力stressでframe 4350に `playerActor1Y` 差分を起こしたため、現時点では単独採用しない。
- `ProcessList + heapScanInterval=900` は6000F後着入力stressを通過した。最終hostは `bytesLast=254,219`, `bytesMax=254,219`, `saveAvgUs=169`, `restoreOps=2`, `restoreAvgUs=11,942`, active fps `59.06`。clientは `saveAvgUs=168`, active fps `59.06`。
- 従来のscan30候補は同stressで `saveAvgUs=431-433us` 程度だったため、正しさを維持しつつ保存コストを約0.17msまで落とせた。

FPS方針:

- 15fps級の遅さは通常性能として扱わない。JITなし、restore diff、scan1、game-state CSV大量出力、`InputNetplayTrace` の長時間runは診断条件。
- 通常性能gateは `-AllowJit` を必須にし、traceなし/軽traceと分けて `active fps` を見る。
- 今回の6000F stressはtrace付きでもhost/client active fps `59.06` なので、現候補はFPS面では実用候補に残す。

次の確認:

- 別input routeとstock touch系で `ProcessList + heapScanInterval=900` を再確認する。
- さらに軽くするなら `heapScanInterval=1800` や、heap scan対象をCode Reference由来のmanager/globalへ寄せる。ただしProcessList-onlyの4350F不一致を踏まえ、fallbackを完全に消すのはまだ早い。

## 2026-06-01 FPS-aware rollback validation

現時点の性能判断では、15fps級の遅さはrollback本体ではなく、主に検証ハーネスを `-AllowJit` なしで回していたことが原因。通常性能を見るrunでは必ず `-AllowJit` を付け、`MELONDS_NSML_ROLLBACK_NSMB_RESTORE_DIFF_TRACE=1`、`scanInterval=1`、game-state CSV大量出力などの診断条件とは分けて扱う。

Current candidate:

- `nsmbtinycore + MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES=1 + MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS=0x200 + MELONDS_NSML_ROLLBACK_NSMB_SCAN_INTERVAL=30`。
- checkpointは約 `253,427` bytes、通常保存平均はおおむね `0.43ms` 前後。
- JIT有効・no draw・no audio sync・game-state traceなしの1800F FPS計測では、baselineがhost/client `59.88/59.96fps`、rollback候補が `59.38/59.40fps`。現候補の通常時overheadはこの条件では約0.5fpsで、15fps級ではない。
- `InputNetplayTrace` 有効やgame-state trace有効のrunは診断用。FPS結論には使わない。

Completed:

- prediction probeをフレーム範囲で絞れる `MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_START_FRAME` / `MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_END_FRAME` を追加した。遅い序盤を変えず、4200F以降などにrollback負荷を寄せられる。
- 入力送信遅延も `MELONDS_NSML_INPUT_SEND_DELAY_START_FRAME` / `MELONDS_NSML_INPUT_SEND_DELAY_END_FRAME` で範囲指定できるようにした。end frameは `0` を「上限なし」として扱う。
- 4200F以降だけ `InputSendDelayFrames=8`、`InputMaxFrameLead=4` の実用寄り条件では、throttleが待って吸収し、5100Fを通過した。ただしactive fpsは約 `49.9fps` まで落ちるため、送信遅延を常時待つ条件は体感評価では別扱い。
- 同じ4200F以降遅延で `InputMaxFrameLead=-1` にしてthrottleを外すと、`RollbackWindow=20` では実input到着が31F後になり `checkpoint missing`。このstressにはwindow 20が不足だった。
- `RollbackWindow=60` に広げた同条件は4500Fを通過した。hostは4200F以降のprobeを含め `restoreOps=3`, `resims=3`, `restoreAvgUs=10,976`, `saveAvgUs=433`, active fps `58.63`。clientも `restoreOps=3`, `resims=3`, `restoreAvgUs=10,475`, active fps `58.87`。
- 同じ `RollbackWindow=60` 条件で5400Fまで延長し、前回問題になった4950F付近もhost/client一致で通過した。最終hostは `bytesLast=253,939`, `saveAvgUs=422`, `restoreOps=2`, `restoreAvgUs=11,286`, `resims=2`, active fps `58.89`。clientは `saveAvgUs=427`, `restoreOps=3`, `restoreAvgUs=10,652`, `resims=3`, active fps `59.06`。
- さらに6000Fまで延長し、結果後の再開周辺もhost/client一致で通過した。最終hostは `bytesLast=253,427`, `bytesMax=254,087`, `saveAvgUs=433`, `restoreOps=2`, `restoreAvgUs=13,236`, active fps `58.91`。clientは `saveAvgUs=431`, `restoreOps=3`, `restoreAvgUs=10,574`, active fps `59.03`。

Current blocker:

- rollback windowは「最大到着遅延 + resim余裕」より短いと、状態の軽さ以前にcheckpointが捨てられて復元できない。診断stressではwindow 60が必要だった。
- 以前の5400F manual seed `0x7A7950E5` では、scan1でも直らないlate object divergenceが出た。今回のwindow 60 + targeted delay stressでは5400F通過したため、現時点ではcheckpoint範囲漏れより「window不足や診断条件差」の可能性が上がった。
- 実用FPSのgateは `-AllowJit`、no restore diff、scan30で見る。restore diff、scan1、InputNetplayTraceつき長時間runは診断stressとして分離する。

Next actions:

- `RollbackWindow=60` + targeted late delay/probeを別input routeへ広げ、ルート依存のspawn/despawnで状態が崩れないか確認する。
- 再発した場合だけ `MELONDS_NSML_ROLLBACK_NSMB_RESTORE_DIFF_TRACE=1` を短い範囲で入れ、未復元Main RAM pageか、Main RAM外のcore進行状態かを切り分ける。
- FPS測定はbaseline/rollbackを同じ `-AllowJit` 条件で並べ、診断traceの遅さを通常性能として扱わない。

## 2026-06-01 previous practical validation

現候補は引き続き `nsmbtinycore + MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES=1 + MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS=0x200 + MELONDS_NSML_ROLLBACK_NSMB_SCAN_INTERVAL=30`。軽量checkpointは約253KB、保存平均は0.39-0.46ms程度、1フレームresim付き復元は7.5-8.7ms程度で推移している。

Completed:

- 手動host/clientルートを5400フレームまで延長し、prediction probe modulo 10、stable-field比較、settle window 60で通過した。host最終は `bytesLast=253,427`, `saveAvgUs=394`, `restoreOps=3`, `restoreAvgUs=7,535`。client最終は `bytesLast=253,427`, `saveAvgUs=396`, `restoreOps=1`, `restoreAvgUs=7,657`。
- `nsmb_us_direct_mvl_both_different.inputs` をhost/client両方に使う別ルートで4200フレームを通過した。途中の `movingHazardX` 差分はsettle window内で収束した。hostは `restoreOps=4`, `restoreAvgUs=8,136`, `saveAvgUs=400`。
- 固定の1770->2220移動確認が別ルート検証の邪魔になるため、`scripts/run-nsmb-mvl-split-local-input-smoke.ps1` に `-SkipMovementProbe` を追加した。game-state同期比較とルート固有の移動probeを分離できる。
- star collectルートはrollbackあり/なしの両方でframe 5880に `playerActor0X` 差分が出たため、現時点ではrollback候補の復元漏れではなくルートまたは比較条件側のbaseline差分として扱う。
- stock touchルートはrollbackなしbaseline 2800フレーム、rollbackあり自然jitter 3200フレームを通過した。自然jitterでは `bytesLast=252,691`, `saveAvgUs=390-399`, `restoreOps=0`。
- stock touch + prediction probe modulo 10はframe 2610付近でmoving hazard差分を起こした。ログ上は強制probeが連続し、通常WANより厳しいstressになっている。診断用に `MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_LIMIT` を追加し、強制prediction mismatch注入回数を上限付きにできるようにした。
- stock touch + prediction probe limit 1は3200フレームを通過した。hostは `restoreOps=2`, `restoreAvgUs=7,470`, `saveAvgUs=395`, `predProbe=1`。
- stock touch + seed固定 + prediction probe limit 6 のrestore diffで、frame 2220/2320復元時に `0x02095400` / `0x02095500` の未復元ページを確認した。`0x02095300` rangeを `0x300` に広げた後、同じseed固定2800フレームと非固定seed 3200フレームのstock touch + limit 6が通過した。非固定seed 3200ではhost側 `bytesLast=253,203`, `bytesMax=253,427`, `saveAvgUs=449`, `restoreOps=1`, `restoreAvgUs=7,573`。

Current blocker:

- 実用候補としてはかなり軽く、複数ルートの自然jitterとstock touchのlimit 6強制probeまで通るようになった。まだ全予測外れstressやより長いルートは未解決なので、実用gateと診断stressを分けて継続確認する。
- star collectルートはbaseline自体が同期比較に合っていないため、rollback検証用ルートとして使うには比較フィールドまたはルート期待値の再設計が必要。

Next actions:

- 追加した `0x02095300+0x300` が他ルートでも安定するか、5400フレーム級の既存ルートと別ルートで再確認する。
- 強制probe limitを段階的に上げ、次に未復元ページが出るか、Main RAM外の進行状態が問題になるかを切り分ける。
- 自然jitterでの長時間検証を増やしつつ、全予測外れstressは実用gateではなく診断stressとして扱う。

## 2026-06-01 latest rollback snapshot focus

ユーザー指示により、delay方式とのhybrid検討はいったん外し、軽いcheckpoint/snapshotとして現実的なrollback方式を実験している。

現在の最有力候補は `nsmbtinycore + MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES=1 + MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS=0x200`。これはMain RAM全体や通常savestateではなく、NSMB向けMain RAM range snapshotに、CPU/timer/scheduler/DMA/IRQ/IPC/WRAMなどの小さいcore進行状態と、GPU3DのFIFO/matrix/pipeline/register系だけを足す方式。checkpoint sizeは最新range補強後で `252,915` bytesまでに収まり、`savestate` や `corelite` よりかなり実用寄り。

ただし、まだ完全な「ROM解析でactor/global構造を静的に確定した案D」ではない。現在のrange setは、coredelta/restore diffと実行時Main RAM観測で見つけたNSMB global/actor/heap周辺を使っている。ROMの関数・構造体・actor tableを本格的に逆引きして、必要状態を名前付き構造として確定する作業はまだ途中ではなく、これからの段階。

今回の追加実験では、`MELONDS_NSML_ROLLBACK_NSMB_SCAN_INTERVAL` を追加し、動的object/range探索結果を数十フレーム単位でキャッシュできるようにした。`scanInterval=30` では、`0x200` 候補のcheckpoint bytesは約 `253KB`、保存平均時間は約 `7.9ms` から約 `0.39-0.41ms` まで下がった。

実rollback検証用に `MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_MODULO` も追加した。これはテスト時だけ予測remote inputを周期的に1bit外し、通常のprediction mismatch/resimulate経路を強制する。restore diffで見つけた未復元ページを追加し、game/global周辺とheap/object周辺を数KB補強した。

`scripts/run-nsmb-mvl-split-local-input-smoke.ps1` には `-IgnoreSpeculativeInputFields` とsettle window検索を追加した。rollback中のgame-state traceは、次フレームでresimulateされるspeculative input状態を含むことがあるため、入力保持/pressedフィールドとsettle後のactor/object/score比較を分けて評価できるようにした。

Verification:

- Build: `cmake --build --preset release-windows-x86_64 --parallel` passed.
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-delayjitter-2600-20260601`: 2600-frame split local-input smoke passed. Client frame 2520: `bytesLast=247,355`, `saveAvgUs=391`, `scanInt=30`, `scanRefresh=55`, `scanCacheHits=1595`, `mismatches=0`.
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-delayjitter-3600-20260601`: 3600-frame split local-input smoke passed. Client frame 3600: `bytesLast=247,355`, `saveAvgUs=393`, `scanRefresh=91`, `scanCacheHits=2639`, `mismatches=0`.
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-restoreprobe-2600-20260601`: 2600-frame smoke passed, but restore probe did not force restoreOps. その後、prediction probeで通常のmismatch/resimulate経路を強制できるようにした。
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-predprobe30-extra-gameglobals-2600-20260601`: prediction probe modulo 30で2600-frame game-state comparison passed。Host側 `restoreOps=2`, `resims=2`, `bytesLast=248,287`, `saveAvgUs=395`, `restoreAvgUs=7,512`。
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-predprobe10-more-diff-ranges-2600-20260601`: prediction probe modulo 10で2600-frame game-state comparison passed。Host側 `restoreOps=2`, `resims=2`, `bytesLast=251,095`, `saveAvgUs=391`, `restoreAvgUs=7,786`。
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-predprobe10-stablefields-extra4-3600-20260601`: prediction probe modulo 10 + stable-field comparison + settle window 60で3600-frame game-state comparison passed。Host側 `restoreOps=3`, `resims=3`, `bytesLast=252,915`, `saveAvgUs=398`, `restoreAvgUs=7,474`。Client側 `restoreOps=1`, `resims=1`, `saveAvgUs=398`, `restoreAvgUs=7,518`。
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-predprobe-all-more-diff-ranges-2600-20260601`: prediction probe modulo 1はframe 1890で `inputPlayer0Held` のspeculative差分によりwrapper比較が停止した。全予測外れstressは現実的WANより過剰だが、追加range探索用の負荷として残す。
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-predprobe-all-stablefields-settlewindow-extra3-2600-20260601`: prediction probe modulo 1 + stable-field comparisonでもframe 1950でplayer位置差が残った。全予測外れはまだ未達。

Current blocker:

- 実用候補としてはかなり近づいたが、まだ「実行時delta-discovered range + 小さいemulator進行状態」。ROM解析でNSMB actor/global構造を確定している段階ではない。
- `scanInterval=30` は現ルートで成功しているが、spawn/despawnや別ルートでrangeが変わる場面の安全余裕は追加検証が必要。
- 全予測外れstressでは、入力保持フィールドなどspeculative状態の比較で止まる。実用評価では、通常WAN相当のprediction頻度、settle後のactor/object/score収束、体感カクつきの確認を分けて測る必要がある。

Next actions:

- prediction probe modulo 10程度を継続stressとして使い、別input routeやさらに長時間で `253KB / saveAvg 0.4ms / restoreAvg 7.5-8ms前後` が維持できるか測る。
- delta-discovered rangeをROM/メモリ解析に戻し、NSMB global/actor/manager/camera/RNG相当へ名前付きで切り分ける。
- `scanInterval` のデフォルト値を上げてよいかは、別ルート・長時間・spawn/despawn検証後に判断する。現時点ではデフォルト1で保守的にしている。

## 2026-06-01 current experiment status

ユーザー指示により、delay方式とのhybrid検討はいったん外し、軽いcheckpoint/snapshotが作れるかだけに焦点を戻した。

Completed:

- 案C寄りのPoCとして、通常savestate互換を捨てたrollback専用 `corelite` backendを追加した。
- `melonDS::NDS::DoRollbackSavestate()` を追加し、通常 `DoSavestate()` がNTRでも常に保存していた16MB Main RAMを、実際の `MainRAMMask + 1` だけ保存するようにした。その他のCPU、DMA、timer、scheduler、GPU/SPU/Wifi等の既存savestate対象は維持している。
- `MELONDS_NSML_ROLLBACK_BACKEND=corelite` / `-RollbackBackend corelite` でPoC rollback backendを選べる。
- `coresparse` backendを追加し、Main RAMのゼロページを省略できるかを試した。
- `coredelta` backendを追加し、keyframeのMain RAMを基準に、各checkpointでは変更ページだけを保存できるようにした。
- 案D寄りのサイズ探索として `nsmbranges` backendを追加した。NSMBのplayer/global/net/stage周辺と検出できる主要actor/object風メモリ範囲だけを保存する。
- `MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL` でdelta keyframe間隔、`MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE` でMain RAMページサイズを調整できる。
- rollback traceへ checkpoint byte/time stats、delta/keyframe数、Main RAM base copy量、delta page size を追加した。

Verification:

- Build: `cmake --build --preset release-windows-x86_64 --parallel` passed.
- Core-lite rollback probe: `logs/codex-rollback-corelite-trace-20260601` passed short split local-input smoke with artificial input delay, prediction mismatches, and resimulation.
- `corelite` checkpoint size was stable at `6,645,137` bytes.
- Same short probe with normal `savestate` backend in `logs/codex-rollback-savestate-trace-20260601` showed `19,228,045` bytes.
- This is about 65% smaller than full savestate, mainly by removing unused NTR upper Main RAM from rollback checkpoints.
- `logs/codex-rollback-corelite-gamestate-20260601` ran both host/client to frame 1500 with game-state traces and rollback resimulation, but the wrapper-level comparison failed at the outer movement-probe check because the short run did not provide the expected movement probe rows. The inner host/client route smoke completed and no rollback restore/resim failure was logged.
- Longer game-state comparison: `logs/codex-rollback-corelite-gamestate-2600-20260601` passed 2600-frame split local-input smoke with game-state comparison enabled. It exercised prediction mismatches and rollback resimulation. Final client-side trace at frame 2520 showed 10 mismatches and 10 resimulations, with checkpoint size still `6,645,137` bytes.
- Timing probe: `logs/codex-rollback-corelite-timing-20260601` showed `corelite` save average around `4.5ms` and restore average around `15-18ms` in the short JIT-enabled synthetic run. `logs/codex-rollback-savestate-timing-20260601` showed normal `savestate` save average around `9.4-9.6ms` and restore average around `19.6ms` under the same style of run.
- `coresparse` timing probe: `logs/codex-rollback-coresparse-timing-20260601` passed, but size was only reduced to `6,054,749` bytes. Save average was around `5.0ms`; zero-page省略だけでは効果が小さい。
- `coredelta` keyframe interval 10: `logs/codex-rollback-coredelta-k10-timing-20260601` passed. Delta checkpoint was around `2.53-2.55MB`, average was around `2.95MB`, restore average was around `18-23ms`.
- `coredelta` keyframe interval 20: `logs/codex-rollback-coredelta-k20-timing-20260601` passed. Average was around `2.75MB`; restore average remained around `21-23ms`.
- `coredelta` keyframe interval 30 with 4KB page: `logs/codex-rollback-coredelta-k30-timing-20260601` passed. Average was around `2.67MB`; delta size was still around `2.53-2.55MB`.
- `coredelta` keyframe interval 30 with 1KB page: `logs/codex-rollback-coredelta-k30-page1024-timing-20260601` passed. Delta size was around `2.48-2.50MB`; average was around `2.62MB`.
- `coredelta` keyframe interval 30 with 256B page: `logs/codex-rollback-coredelta-k30-page256-timing-20260601` passed. Delta size was around `2.46-2.47MB`; average was around `2.60MB`.
- Longer game-state comparison for best current candidate: `logs/codex-rollback-coredelta-k30-page256-gamestate-2600-20260601` passed 2600-frame split local-input smoke with game-state comparison enabled. Host/client both exercised prediction mismatches and resimulation without restore failure. Final traces around frame 2520 showed average checkpoint bytes around `2.60MB`, save average around `3.4-3.5ms`, restore average around `18ms`.
- `nsmbranges` short timing probe: `logs/codex-rollback-nsmbranges-timing-20260601` passed without game-state comparison. Checkpoint size was around `58KB`, restore average was around `0.2-0.6ms`, but save average was around `6.6ms` because the PoC scans Main RAM for objects on every checkpoint.
- `nsmbranges` 2600-frame game-state comparison with the first fixed range set: `logs/codex-rollback-nsmbranges-gamestate-2600-20260601` failed at frame 1950 (`playerActor1X` mismatch).
- `nsmbranges` with all scanned object-like ranges: `logs/codex-rollback-nsmbranges-allobjects-gamestate-2600-20260601` still failed, now at frame 930 (`playerActor0Y` mismatch). Checkpoint size stayed small at around `62-64KB`; restore stayed below `1ms`, but correctness was insufficient.
- `nsmbcoreranges` diagnostic backend was added to split the failure cause. It saves melonDS core state with Main RAM skipped, then applies the NSMB range snapshot. Short timing probe `logs/codex-rollback-nsmbcoreranges-timing-20260601` passed without game-state comparison. Size was around `2,513,397` bytes, save average around `11ms`, restore average around `12-14ms`.
- `nsmbcoreranges` 2600-frame game-state comparison `logs/codex-rollback-nsmbcoreranges-gamestate-2600-20260601` still failed at frame 930 (`playerActor0Y` mismatch). Restoring core state did not fix the failure.
- `nsmbcoreranges` with broad diagnostic ranges (`MELONDS_NSML_ROLLBACK_NSMB_WIDE_RANGES=1`, adding `0x02080000..0x020E0000` and `0x023C0000..0x02400000`) also failed at frame 930 in `logs/codex-rollback-nsmbcoreranges-wide-gamestate-2600-20260601`. Size rose to around `3,144,901` bytes, but correctness did not improve.
- `coredelta`の成功経路に `MELONDS_NSML_ROLLBACK_DELTA_PAGE_TRACE` を追加し、Main RAM差分ページを256B単位で出せるようにした。930フレーム前後では、既存NSMB rangeが `0x0208xxxx` のgame/global、`0x0219xxxx`/`0x021Bxxxx`/`0x02288400` 付近のheap/object、`0x023FFC00` 付近を取り逃がしていた。
- `MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES=1` を追加し、delta/restore diffで見つけた小さな追加rangeをNSMB snapshotへ反映できるようにした。
- `MELONDS_NSML_ROLLBACK_NSMB_RESTORE_DIFF_TRACE=1` を追加し、NSMB range復元直後に診断用Main RAM shadow copyと比較して、未復元ページを直接出せるようにした。
- 追加rangeの初回反映だけでは `nsmbcoreranges` は同じ930フレームで失敗したが、restore diffで `0x02085B00`、`0x02088000`、`0x021B4B00` などの未復元ページを追加した後、`logs/codex-rollback-nsmbcoreranges-delta-discovered-more-heap-gamestate-2600-20260601` が2600-frame split local-input smokeを通過した。最終traceは checkpoint bytes `2,534,821`、save average 約`11.0ms`、restore average 約`11.1ms`。
- 同じ追加rangeで `nsmbranges` 単体も試したが、`logs/codex-rollback-nsmbranges-delta-discovered-more-heap-gamestate-2600-20260601` は1290フレームの入力状態で不一致になった。入力rangeを外す `MELONDS_NSML_ROLLBACK_NSMB_SKIP_INPUT_RANGES=1` でも `logs/codex-rollback-nsmbranges-delta-discovered-skip-input-gamestate-2600-20260601` は同じ1290付近で不一致になった。
- `MELONDS_NSML_ROLLBACK_CORE_SKIP_MASK` を追加し、`nsmbcoreranges` のMain RAM以外core stateからCart/GPU/SPU/Mic+SPI+RTC/Wifiを実験的に外せるようにした。`0x08`（Mic/SPI/RTC skip）は `logs/codex-rollback-nsmbcoreranges-core-skip-0x08-gamestate-2600-20260601` で2600-frame smokeを通過したが、サイズは約`2.54MB`のままで実用上の削減はほぼなかった。`0x02`（GPU skip）は1290フレーム、`0x04`（SPU skip）は1950フレーム、`0x0E`（GPU+SPU+Mic/SPI/RTC skip）は1620フレームで不一致になった。
- `nsmbtinycore` backendを追加した。NSMB range snapshotに、CPU/timer/scheduler/DMA/IRQ/IPC/WRAMなどの小さいcore stateだけを足す実験用backendで、通常savestate互換からさらに離れて案Dへ寄せるための切り分け。
- `nsmbtinycore + delta-discovered ranges` は checkpoint size 約`238KB`まで下がったが、1290フレームで `playerActor0Y` が不一致になった。GPU/SPU等の大きいdevice stateを完全に捨てるにはまだ足りない。
- `MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS=1`（GPU timing/2D registerだけ追加）でも1290フレームで不一致。`=2`（full GPU追加）は1950フレームまで進んで `playerActor0X` 不一致になった。`=6`（full GPU+SPU追加）は `logs/codex-rollback-nsmbtinycore-fullgpu-spu-gamestate-2600-20260601` で2600-frame smokeを通過したが、checkpoint sizeは約`2.49MB`で `nsmbcoreranges` から約50KBしか減らない。
- GPU subset診断を追加し、`MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS` の高位bitで palette/OAM、VRAM、full GPU3D、light GPU3D を個別保存できるようにした。
- `0x0C4`（SPU + palette/OAM + VRAM）は `logs/codex-rollback-nsmbtinycore-gpuvram-spu-gamestate-2600-20260601` で1620フレーム `movingHazardX` 不一致。sizeは約`916KB`で軽いが、GPU3D状態なしでは不足。
- `0x104`（full GPU3D + SPU、VRAM/palette/OAMなし）は `logs/codex-rollback-nsmbtinycore-gpu3d-spu-novram-nopaloam-gamestate-2600-20260601` で2600-frame smoke通過。sizeは約`1.81MB`。
- `0x200`（light GPU3Dのみ、SPU/VRAM/palette/OAMなし）は `logs/codex-rollback-nsmbtinycore-gpu3dlight-nospu-fixed-gamestate-2600-20260601` で2600-frame smoke通過。checkpoint sizeは `247,355` bytes、save averageは約`7.9ms`。light GPU3DはFIFO、matrix、pipeline、register系を戻すが、VertexRAM/PolygonRAM/RenderPolygonRAMは戻さない。
- 人工送信遅延/jitter付きの `0x200` 追加検証 `logs/codex-rollback-nsmbtinycore-gpu3dlight-delayjitter-gamestate-2600-20260601` も2600-frame smoke通過。client側で `restoreOps=1`、`resims=1` を踏み、sizeは同じ `247,355` bytes、restore averageは約`8.2ms`。

Current blocker:

- 現在の最有力は `nsmbtinycore + delta-discovered ranges + light GPU3D`。これは完全な案D actor/global snapshotではないが、DS全体savestateではなく、NSMB range snapshotにCPU/timer/scheduler/DMA/IRQ/IPC/WRAMとGPU3Dの小さい進行状態だけを足す形なので、かなり案D寄り。
- 2600-frame synthetic routeでは `247,355` bytesまで下がった。まだ実行時diffで見つけたMain RAM rangeに依存しており、ROM静的解析でactor/global構造を完全確定した状態ではない。
- GPU3D lightで通る一方、GPU3Dなしの約`916KB`構成は1620フレームで壊れる。戻すべきなのは描画メモリ本体ではなく、GPU3D FIFO/matrix/pipeline/register系の進行状態らしい。
- SPUは今回の最小候補 `0x200` では不要だった。前のfull GPUのみ失敗との違いは再確認余地があるが、少なくとも現候補ではSPU保存は必須ではない。
- delta/restore diffで発見した範囲は実行時メモリ解析ベースであり、ROM静的解析でactor/global構造を確定した状態ではない。
- Real WAN jitter patterns and longer sessions are not measured yet.

Next actions:

- 次は `0x200` 候補をより長いframe数、別input route、rollback restore probeで検証する。人工遅延/jitterでは復元経路を1回踏んで通ったが、復元回数はまだ少ない。
- 並行して、delta-discovered rangeをROM/メモリ解析へ戻し、`戻すべきNSMB global/actor` と `毎フレーム再注入されるvolatile input/net packet` を分ける。
- `nsmbranges` 単体の案D完全形へ寄せるには、light GPU3Dで戻しているFIFO/matrix/pipeline/register相当のうち、ゲーム進行に本当に効く要素をさらに削る。

この文書は、Mario vs Luigi online PoCで検討したrollback方式の議論を、後で再開できるように分離して残す設計メモ。

## 背景

現在の本線は、`InputDelayFrames=4` 前後の低ディレイ入力同期方式。手動確認では4フレーム遅延なら実用に届く可能性がある。

一方で、高遅延・jitterが大きいWAN環境では、固定4フレーム遅延だけではremote inputが間に合わず、停止やカクつきが出る可能性がある。そのため、rollback方式も将来候補として検討した。

## これまでに試したこと

### melonDS full savestate rollback

既存のmelonDS savestateを使い、過去フレームのcheckpointへ戻して、保存済み入力履歴で現在フレームまで再実行する方式。

良い点:

- 正しさは高い。CPU、RAM、デバイス状態など、melonDSが通常savestateで保持する状態をまとめて戻せる。
- PoC実装は比較的早く作れる。
- `InputDelayFrames=0` でも、remote input未着時に予測入力で進める土台は動いた。

問題:

- 1 checkpointが約19MBあり、保存/復元/再実行が重い。
- rollback発生時に体感で止まる、またはカクつく。
- 同一PCでhost/clientを両方動かす検証では、実用感から遠い場面があった。
- 毎フレームcheckpointは現実的ではなく、checkpoint intervalを広げると再実行距離が伸びる。

現時点の評価:

- 正しさ確認用、または低頻度rollbackの保険としては使える。
- ゼロ遅延rollbackの主力として使うには重い。

### ARM9 Main RAM 4MB snapshot

ARM9 Main RAM最大4MBだけを`memcpy`で保存/復元する軽量backendを試した。公開フレームカウンタとして `NumFrames` / `NumLagFrames` / `LagFrameFlag` も小さいヘッダに入れて復元した。

良い点:

- checkpointが約4MB + 40byteになり、full savestateよりかなり軽い。
- 短距離の保存/復元/resimulate自体は動作した。

問題:

- CPUレジスタ、timer、DMA、scheduler、VRAM、Wi-Fi、IPCなどが戻らない。
- 人工送信遅延6フレーム + jitter4の検証で、rollback後にhost側のmoving hazardが止まり、client側だけ進む不一致が出た。
- RAMだけでは「過去のエミュレータ状態」ではなく、「過去の一部メモリを現在のCPU状態へ貼り直した状態」になってしまう。

現時点の評価:

- 軽いが正しさ不足。
- 実用候補ではない。
- ここから正しくするには、結局core側の状態をかなり追加保存する必要がある。

## 検討したrollback案

### Tango調査から得た示唆

`external/tango` にTango本体をcloneして、`tango-pvp` のrollback実装を確認した。

Tangoの重要な構造:

- ゲームごとのROM hook/trapを持ち、通信処理、入力読み取り、round開始/終了、RNG初期化などをゲーム別に差し替える。
- live primary emulatorとは別に、remote peerをローカルで再現する `shadow` emulator を持つ。
- 再実行専用のheadless `Fastforwarder` emulatorを持つ。描画を飛ばして高速に再実行する。
- `settled_state` は実remote inputで確定済みの単一checkpointとして保持する。
- `speculative tail` は `settled_state` から一時的にfastforwardして表示用stateを作る。ここで作った予測stateを次のseedへ混ぜない。
- ユーザー設定のframe delayを、両者共通の `input_delay = min(local, remote)` と、各ローカルだけの `presentation_delay = local - input_delay` に分ける。
- 入力はwire上ではraw inputを送る。local側ではdelay line、remote側ではqueue prefillで同じ共有input delayを実現する。
- 先行しすぎた側だけFPS targetを下げるthrottlerを持ち、双方が無制限にズレていくのを防ぐ。

Tangoで特に参考になる点:

1. rollbackを「毎回過去へ戻る処理」ではなく、`settled checkpoint` から表示用stateを毎フレーム作る仕組みにしている。
2. 予測stateを確定checkpointに混ぜない。確定checkpointは実inputだけで進める。
3. 共有input delayを使って、rollback深度そのものを先に削っている。
4. presentation delayはローカル表示だけの問題として扱い、ネットワーク上のtickとは分離している。
5. round lifecycleを明示的に管理し、roundをまたいだ古いinputを捨てる。
6. remote packet予測はゲームごとのpacket構造を理解した上で行っている。

NSMBへの適用可能性:

- `input_delay + presentation_delay` 分割は、そのまま採用する価値が高い。
- `settled checkpoint` と `speculative display state` を分ける設計も採用候補。
- 先行側だけを緩やかに減速するthrottlerは、host/clientのframe lead制御より自然にできる可能性がある。
- `shadow emulator` はDSだとコストが高い。NSMBの場合、今は「remote packetを再生成する」より「remote inputを同じゲームへ入れる」構造なので、Tangoのshadowをそのまま持ち込む必要は薄い。
- TangoのmGBA stateはGBAなので軽い。一方melonDS savestateは約19MBあり、同じ頻度で使うと重い。ここはそのまま真似できない。

NSMB向けに取り込むなら、次の順が現実的:

```text
1. 現在の低ディレイ方式を、Tango風に input_delay / presentation_delay に整理する。
2. 現在の InputMaxFrameLead を、Tango風の frame advantage + throttler に置き換えるか比較する。
3. rollbackを使う場合も、確定checkpointは実remote inputだけで進める。
4. 予測stateを次のcheckpointへ混ぜないルールを徹底する。
5. full savestate rollbackは短距離・低頻度に限定する。
```

現時点の判断:

- Tangoは「ゼロ遅延rollbackを力技で回している」のではなく、input delay、presentation delay、settled checkpoint、speculative tail、throttlingを組み合わせてrollback深度を管理している。
- これは今のNSMB方針と相性がよい。
- ただし、TangoはGBAでsavestateが軽く、ゲーム別通信packetもかなり解析済み。DS/NSMBへそのまま移植はできない。
- 参考にすべきなのはコードの部品より、`settled checkpointを汚さない`、`rollback深度をinput delayで削る`、`先行側をthrottleする` という設計。

### 案A: ゼロ遅延full rollback

`InputDelayFrames=0`で常に即時反映し、remote inputが後から違っていたらrollbackする。

評価:

- 操作感は理想に近い。
- ただしrollback頻度が高くなりやすい。
- DSエミュ全体のsavestateが重いため、現状ではカクつきが大きい。
- 快適化するには、かなり深いcheckpoint最適化が必要。

結論:

- 最終的にできれば強いが、今の実装難度とリスクは高い。

### 案B: 小入力遅延 + 小rollback

`InputDelayFrames=3〜4`を残し、通常はremote inputが間に合うようにする。packetが少し遅れた時だけ、最大4〜6フレーム程度を予測入力で進め、後着入力が違っていた場合だけ短距離rollbackする。

想定動作:

```text
通常:
  3〜4フレーム遅延で入力を適用する
  ほとんどのpacketは間に合うのでrollbackしない

packetが少し遅れた時:
  1〜4フレームだけ予測入力で進める
  後から本物の入力が来たら短距離rollbackする

大きく遅れた時:
  rollbackし続けず、一時停止して待つ
```

評価:

- 実装難度と実現性のバランスが最も良い。
- rollback頻度とrollback距離を小さくできる。
- full savestate backendでも、発生頻度を抑えれば体感カクつきを許容範囲にできる可能性がある。
- 国内WANの安定回線では、4フレーム遅延で大半の入力が間に合う見込みがある。

結論:

- rollbackを主役にせず、低ディレイ方式の保険にする。
- 将来rollbackを再開するなら、この案が第一候補。

候補設定:

```text
InputDelayFrames: 3〜4
InputMaxFrameLead: 4〜6
MaxRollbackFrames: 4〜6
RollbackBackend: savestate
RollbackCheckpointInterval: 1〜2
InputUnreliable: enabled
InputBundleHistory: 8〜12
Rollback over limit: stall
```

### 案C: core側の軽量checkpoint API

melonDS core側に、rollback専用の軽量checkpoint APIを作る。通常savestateと同じ正しさを目指しつつ、ファイル互換性、圧縮、不要メタデータなどを削り、必要な内部状態だけを高速保存/復元する。

必要になりうる状態:

- ARM9/ARM7 CPU state
- timers
- DMA
- IRQ
- scheduler/event queue
- Main RAM / WRAM / VRAM / OAM / palette
- IPC/FIFO
- Wi-Fi
- SPUのゲーム進行に影響する部分
- JIT cache invalidation policy

評価:

- 正しくできれば最もきれい。
- ただしmelonDS coreへの深い改造になる。
- 何か1つ漏れるとrollback後に不一致が出る。
- 実装・検証コストは高い。

結論:

- 中長期候補。
- まず小入力遅延 + 小rollbackでfull savestateを使い、どうしても重い場合に検討する。

### 案D: NSMBゲーム状態snapshot

DS全体ではなく、NSMB MvsLのゲーム側状態だけをsnapshotする。たとえばplayer actor、敵、Big Star、coin/item、RNG、MvsL global stateなどを保存/復元する。

必要になりうる状態:

- Mario/Luigi actor状態、座標、速度、アニメーション、死亡/復帰状態
- 敵、ブロック、土管、スター、コイン、アイテム、エフェクト
- object manager / actor list / spawn/despawn状態
- collision/physics内部状態
- MvsL score、残機、勝敗、timer、stage state
- RNG state
- input/communication tick
- camera、HUD、sound/event queueの一部

良い点:

- 成功すれば非常に軽い。
- NSMB MvsL専用に割り切れる。
- DS core全体のrollbackよりゲーム目的に近い。

問題:

- 解析難度が高い。
- 漏れた状態が1つあるだけで数秒後にズレる。
- ROM/メモリ構造への依存が強くなる。
- actor listやspawn/despawn管理を完全に理解する必要がある。

現実的なPoC順:

```text
1. player actor 2体
2. Big Star actor
3. moving hazard / enemy actor数体
4. RNG state
5. MvsL global state
```

結論:

- DS core軽量checkpointとは別方向の中長期候補。
- 「NSMB MvsL専用ゲーム状態rollback」を作る覚悟が必要。
- 今すぐ本線にするより、低ディレイ方式が限界に達した後の研究対象。

## 現時点の推奨方針

最終目標が「快適なWAN越し対戦」なら、現時点の最有力は次のハイブリッド方針。

```text
国内・安定回線:
  3〜4F delay + packet bundle + ほぼrollbackなし

不安定な瞬間:
  最大4〜6Fだけrollbackで吸収

それ以上の遅延:
  rollbackし続けず、一時停止して同期維持
```

理由:

- 4フレーム入力遅延は手動確認で実用に届く可能性がある。
- rollbackを常用しないため、full savestateの重さを避けやすい。
- packet bundleと組み合わせると、短いpacket lossやjitterは吸収できる可能性が高い。
- 実装が現実的で、現在のPoCから段階的に進められる。

避けたい方針:

- `InputDelayFrames=0` を前提にした常時rollback。
- ARM9 RAMだけを戻す不完全rollback。
- いきなりcore全体の軽量checkpointを作る。
- いきなりNSMBゲーム状態snapshotを完全実装する。

## 後で再開する場合の次アクション

1. 低ディレイ本線で `InputDelayFrames=3/4/5` の実用性を実2PCまたはLAN分散で測る。
2. `MaxRollbackFrames` を明示的に導入し、rollback距離を4〜6フレームに制限する。
3. rollback距離超過時はstallへ落とす。
4. full savestate backendのまま、小rollbackだけで体感カクつきが許容範囲か確認する。
5. それでも重い場合だけ、core軽量checkpointまたはNSMBゲーム状態snapshotのPoCへ進む。

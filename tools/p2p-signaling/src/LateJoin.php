<?php
declare(strict_types=1);

/** Requests do not confer membership. Only the authenticated host can issue a name-bound grant. */
trait LateJoinSignaling
{
    private static function sweepJoinRequests(array $state, int $now): array
    {
        foreach (($state['joinRequests'] ?? []) as $id => $r) {
            if ($now-(int)$r['createdAt']>300000 || $now-(int)$r['lastSeen']>45000) {
                if (isset($r['grant'])) unset($state['grants'][hash('sha256',$r['grant'])]);
                unset($state['joinRequests'][$id]);
            }
        }
        return $state;
    }

    public function requestLateJoin(string $roomId, string $code, array $claims, string $name, bool $spectate=false): string
    {
        $ticket=$roomId.Store::randomHex(28); $id=hash('sha256',$ticket); $now=$this->store->now();
        return $this->store->withLock(self::file($roomId), function(array $s) use($ticket,$id,$now,$code,$claims,$name,$spectate): array {
            if (!$s || ($s['closed']??false) || $s['code']!==Store::normalizeRoomCode($code)
                || (($claims['publicOnly']??false) && $s['visibility']!=='public'))
                throw new ServiceError(404,'room_not_found','That game is no longer available.');
            if ($s['appVersion']!==$claims['appVersion'])
                throw self::versionMismatchError($s['appVersion'],$claims['appVersion']);
            if ($s['gameProtocol']!==(int)$claims['gameProtocol'] || $s['contentHash']!==$claims['contentHash'])
                throw new ServiceError(409,'content_mismatch','This game needs matching game and mod files.');
            if (!($s['allowLateJoin']??false) || $s['phase']!=='match' || !empty($s['joinWindow'])
                || !isset($s['peers'][(string)$s['hostPeerId']]) || $now-(int)$s['peers'][(string)$s['hostPeerId']]['lastSeen']>=45000)
                throw new ServiceError(409,'join_closed','This game is not accepting join requests.');
            $s=self::sweepJoinRequests($s,$now);
            if (count($s['joinRequests'])>=8 || self::reservedSeats($s)>=(int)$s['maxPeers'])
                throw new ServiceError(409,'room_full','This game has no space for another player.');
            foreach($s['peers'] as $p) if($p['name']===$name)
                throw new ServiceError(409,'name_taken','Somebody in that game is already using your player name. Change Player name in Settings, then try again.');
            foreach($s['joinRequests'] as $r) if($r['name']===$name && in_array($r['state'],['pending','approved'],true))
                throw new ServiceError(409,'request_pending','A request with that name is already waiting.');
            unset($claims['publicOnly']);
            $s['joinRequests'][$id]=['name'=>$name,'spectator'=>$spectate,'claims'=>$claims,'state'=>'pending','createdAt'=>$now,'lastSeen'=>$now];
            return [$s,$ticket];
        });
    }

    public function lateJoinStatus(string $ticket, array $claims, bool $cancel=false): array
    {
        $roomId=self::roomIdFromSession($ticket); $id=hash('sha256',$ticket); $now=$this->store->now();
        return $this->store->withLock(self::file($roomId), function(array $s) use($id,$claims,$cancel,$now): array {
            if (!$s || ($s['closed']??false)) throw new ServiceError(404,'room_not_found','That game has ended.');
            $s=self::sweepJoinRequests($s,$now); $r=$s['joinRequests'][$id]??null;
            if (!$r || $r['claims']!==$claims) throw new ServiceError(403,'request_expired','That join request has expired.');
            if ($cancel) {
                if(isset($r['grant'])) unset($s['grants'][hash('sha256',$r['grant'])]);
                $r['state']='cancelled';
            }
            $r['lastSeen']=$now; $s['joinRequests'][$id]=$r;
            $answer=['requestState'=>$r['state']];
            if($r['state']==='approved') {
                if(!isset($s['grants'][hash('sha256',$r['grant'])])) $answer['requestState']='expired';
                else $answer+=['grant'=>$r['grant'],'code'=>$s['code'],'maxPeers'=>$s['maxPeers'],'visibility'=>$s['visibility']];
            }
            return [$s,$answer];
        });
    }

    /** Host queue/decision endpoint. Approval happens only after peers have paused directly. */
    public function manageLateJoin(string $token, string $action, string $id=''): array
    {
        $roomId=self::roomIdFromSession($token); $now=$this->store->now();
        return $this->store->withLock(self::file($roomId), function(array $s) use($token,$action,$id,$now): array {
            if(!$s) throw new ServiceError(404,'room_not_found','That game has ended.');
            $who=self::authenticate($s,$token,$now);
            if(in_array($action,['request_play','cancel_play','play_status'],true)) {
                $peerId=(string)$who['peerId'];
                if((int)$s['gameProtocol']<9 || $who['peerId']===(int)$s['hostPeerId'])
                    throw new ServiceError(403,'forbidden','Only a spectator can request to play.');
                $s=self::sweepJoinRequests($s,$now);
                $found=null;
                foreach($s['joinRequests'] as $key=>$r) if(($r['existingPeer']??0)===$who['peerId']) $found=$key;
                if($action==='request_play') {
                    if(!($s['peers'][$peerId]['spectator']??false) || !($s['allowLateJoin']??false)
                        || $s['phase']!=='match' || !empty($s['joinWindow']))
                        throw new ServiceError(409,'join_closed','This game is not accepting requests to play.');
                    if($found===null || $s['joinRequests'][$found]['state']!=='pending') {
                        if($found!==null) unset($s['joinRequests'][$found]);
                        if(count($s['joinRequests'])>=8) throw new ServiceError(409,'join_busy','The host has too many requests waiting.');
                        $found=Store::randomHex(32);
                        $s['joinRequests'][$found]=['name'=>$s['peers'][$peerId]['name'],'spectator'=>false,
                            'existingPeer'=>$who['peerId'],'state'=>'pending','createdAt'=>$now,'lastSeen'=>$now];
                    }
                }
                if($found!==null) {
                    $s['joinRequests'][$found]['lastSeen']=$now;
                    if($action==='cancel_play' && $s['joinRequests'][$found]['state']==='pending') $s['joinRequests'][$found]['state']='cancelled';
                }
                return [$s,[['playRequest',$found===null ? 'none' : $s['joinRequests'][$found]['state']]]];
            }
            if($who['peerId']!==(int)$s['hostPeerId']) throw new ServiceError(403,'forbidden','Only the host can manage join requests.');
            $s['peers'][(string)$who['peerId']]['lastSeen']=$now; $s['lastSeen']=$now;
            $s=self::sweepJoinRequests(self::expireGrants($s,$now),$now);
            if($action==='approve' || $action==='approve_spectator') {
                $r=$s['joinRequests'][$id]??null;
                if(!$r || !($s['allowLateJoin']??false)) throw new ServiceError(409,'request_expired','That request is no longer available.');
                if($r['state']==='approved') return [$s,[]];
                if($r['state']!=='pending' || $s['phase']!=='match' || !empty($s['joinWindow']) || (empty($r['existingPeer']) && self::reservedSeats($s)>=(int)$s['maxPeers']))
                    throw new ServiceError(409,'join_busy','Another join is already being synchronized.');
                if(!empty($r['existingPeer'])) {
                    $peerId=(string)$r['existingPeer'];
                    if($action!=='approve' || !isset($s['peers'][$peerId]) || !($s['peers'][$peerId]['spectator']??false)
                        || $s['peers'][$peerId]['name']!==$r['name']) throw new ServiceError(409,'request_expired','That spectator has left.');
                    $s['joinWindow']=$id; $s['phase']='lobby'; ++$s['epoch'];
                    foreach($s['peers'] as &$p) $p['epoch']=$s['epoch']; unset($p);
                    $s['peers'][$peerId]['spectator']=false;
                    $s['peers'][$peerId]['lateRequest']=$id;
                    $s['joinRequests'][$id]['state']='approved';
                    return [$s,[]];
                }
                $s['joinRequests'][$id]['spectator']=$action==='approve_spectator' || ($r['spectator']??false);
                // Protocol 8 observers are outside the controller roster. Admitting one must
                // not invalidate player grants, reopen the lobby, or start a match barrier.
                $passive=(int)$s['gameProtocol']>=8 && $s['joinRequests'][$id]['spectator'];
                if(!$passive) {
                    $s['joinWindow']=$id; $s['phase']='lobby'; ++$s['epoch'];
                    foreach($s['peers'] as &$p) $p['epoch']=$s['epoch']; unset($p);
                }
                $grant=$s['id'].Store::randomHex(28);
                $s=self::issueGrant($s,$grant,'client',$r['claims'],$now);
                $s['grants'][hash('sha256',$grant)]['lateRequest']=$id;
                $s['grants'][hash('sha256',$grant)]['name']=$r['name'];
                $s['grants'][hash('sha256',$grant)]['spectator']=$passive;
                $s['joinRequests'][$id]['state']='approved'; $s['joinRequests'][$id]['grant']=$grant;
            } elseif($action==='decline') {
                if(isset($s['joinRequests'][$id]) && $s['joinRequests'][$id]['state']==='pending') {
                    if(!empty($s['joinRequests'][$id]['existingPeer'])) $s['joinRequests'][$id]['state']='declined';
                    elseif((int)$s['gameProtocol']>=7) $s['joinRequests'][$id]['spectator']=true;
                    else $s['joinRequests'][$id]['state']='declined';
                }
            } elseif($action==='abort') {
                if(($s['joinWindow']??'')===$id) {
                    foreach($s['peers'] as $peerId=>$p) if(($p['lateRequest']??'')===$id) $s=self::removePeer($s,(int)$peerId,$now);
                    $s['grants']=[]; $s['joinWindow']=''; $s['phase']='match'; ++$s['epoch'];
                    foreach($s['peers'] as &$p) $p['epoch']=$s['epoch']; unset($p);
                }
                if(isset($s['joinRequests'][$id])) $s['joinRequests'][$id]['state']='declined';
            } elseif($action!=='list') throw new ServiceError(400,'bad_request','Unknown join request action.');
            $lines=[];
            foreach($s['joinRequests'] as $key=>$r) if($r['state']==='pending') $lines[]=['request',$key.'|'.bin2hex($r['name']).((int)$s['gameProtocol']>=7 ? '|'.(($r['spectator']??false)?'spectator':'player') : '')];
            return [$s,$lines];
        });
    }
}

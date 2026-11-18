"""Real HTTP smoke check. Run with Python 3 and the built quizsphere_web path.

Uses only the standard library and a disposable database; never real user data.
"""
import http.client
import json
import os
import pathlib
import sqlite3
import subprocess
import sys
import tempfile
import time
import urllib.parse

exe=pathlib.Path(sys.argv[1]).resolve()
port=int(sys.argv[2]) if len(sys.argv)>2 else 8876
origin=f'http://127.0.0.1:{port}'
def call(path,data=None,identity=None,origin_value=origin,host=None):
    headers={}
    if identity:headers.update({'Cookie':identity[0],'X-CSRF-Token':identity[1]})
    if data is not None:headers.update({'Origin':origin_value,'Content-Type':'application/x-www-form-urlencoded'})
    if host:headers['Host']=host
    c=http.client.HTTPConnection('127.0.0.1',port,timeout=4)
    c.request('POST' if data is not None else 'GET',path,urllib.parse.urlencode(data) if data is not None else None,headers)
    response=c.getresponse();raw=response.read();cookie=response.getheader('Set-Cookie');status=response.status
    c.close();return status,json.loads(raw) if path.startswith('/api/') else raw,cookie
def ok(path,data=None,identity=None):
    status,result,cookie=call(path,data,identity);assert status==200,(path,status,result);return result,cookie
def login(name):
    body,cookie=ok('/api/login',{'username':name,'password':'DemoPass123!'});return cookie,body['csrf']
with tempfile.TemporaryDirectory(prefix='quizsphere-http-') as directory:
    db=pathlib.Path(directory)/'demo.sqlite'
    def start():
        p=subprocess.Popen([str(exe),'--port',str(port),'--db',str(db)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        for _ in range(50):
            try:
                if call('/')[0]==200:return p
            except OSError:pass
            if p.poll() is not None:raise AssertionError(p.stderr.read())
            time.sleep(.1)
        p.terminate();raise AssertionError('Server did not become ready')
    p=start()
    try:
        assert call('/api/quizzes')[0]==401
        assert call('/api/register',{'username':'evil','password':'DemoPass123!','role':'Student'},origin_value='http://evil.example')[0]==403
        assert call('/',host='evil.example')[0]==403
        for name,role in [('teacher','Instructor'),('student','Student'),('other','Student')]:
            ok('/api/register',{'username':name,'password':'DemoPass123!','role':role})
        teacher=login('teacher');student=login('student');other=login('other')
        assert call('/api/questions',identity=student)[0]==403
        assert call('/api/logout',{},(student[0],'wrong'))[0]==403
        ok('/api/questions/save',{'id':'q1','text':'Two plus two?','subject':'Math','difficulty':'Easy','options':'3\n4','correct':1,'available':'true'},teacher)
        quizzes=[]
        for title in ['Manual','Timeout']:
            q,_=ok('/api/quizzes/create',{'title':title,'minutes':1,'count':1,'subject':'Math','difficulty':'Easy','marks':4,'penalty':1},teacher);quizzes.append(q['id'])
        print('PASS HTTP auth, CSRF, host/origin boundary, role denial, question and quiz creation',flush=True)
        manual=quizzes[0]
        state,_=ok('/api/attempt/start',{'quiz':manual,'mode':'practice'},student)
        index=state['options'].index('4')
        state,_=ok('/api/attempt/action',{'action':'answer','index':index},student);assert state['feedback']['correct']
        ok('/api/attempt/action',{'action':'submit','confirmed':'true'},student)
        assert call('/api/results?quiz='+manual,identity=student)[0]==400
        assert ok('/api/rankings?type=all',identity=student)[0]==[]
        state,_=ok('/api/attempt/start',{'quiz':manual,'mode':'graded'},student)
        assert 'answer' not in state and 'correct' not in state and state['feedback'] is None
        ok('/api/attempt/action',{'action':'answer','index':state['options'].index('4')},student)
        assert call('/api/attempt/action',{'action':'submit','confirmed':'false'},student)[0]==400
        state,_=ok('/api/attempt/action',{'action':'submit','confirmed':'true'},student);assert state['report']['score']==4
        assert call('/api/results?quiz='+manual,identity=other)[0]==400
        print('PASS practice isolation, no premature graded key, correct original-option mapping and final result',flush=True)
        state,_=ok('/api/attempt/start',{'quiz':quizzes[1],'mode':'graded'},student)
        ok('/api/attempt/action',{'action':'answer','index':state['options'].index('4')},student)
        # Model time spent offline: stop server, age this disposable persisted start, resume.
        p.terminate();p.wait(timeout=5)
        c=sqlite3.connect(db)
        c.execute("update attempts set started=strftime('%Y-%m-%dT%H:%M:%SZ','now','-57 seconds') where quiz=?",(quizzes[1],));c.commit();c.close()
        p=start();student=login('student')
        result,_=ok('/api/results?quiz='+manual,identity=student);assert result['score']==4
        assert call('/api/attempt/start',{'quiz':manual,'mode':'graded'},student)[0]==400
        ok('/api/attempt/start',{'quiz':quizzes[1],'mode':'graded'},student)
        time.sleep(4)
        state,_=ok('/api/attempt',identity=student)
        assert state['completed'] and state['report']['auto'] and state['report']['score']==4,state
        rows,_=ok('/api/rankings?type=all',identity=student);assert any(r['own'] and r['score']==8 for r in rows)
        print('PASS restart persistence, retake rejection, idle server-tick auto-submit and real rankings',flush=True)
    finally:
        p.terminate();p.wait(timeout=5)
print('All real HTTP smoke checks passed.')

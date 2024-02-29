#include <stdio.h>
#include "csapp.h"
#include "cache.h"

web_object_t *rootp;
web_object_t *lastp;
int total_cache_size = 0;

/*
 * find_cache - 캐시 연결리스트에서 path가 일치하는 객체를 찾아 반환
 */
web_object_t *find_cache(char *path)
{
  if (!rootp)
    return NULL;
  web_object_t *current = rootp;

  while (current->next)
  { // path가 같은 노드를 찾았다면 해당 객체 반환
    if (!strcmp(current->path, path))
      return current;
    current = current->next;
  }
  return NULL;
}

/*
 * send_cache - 캐시된 Response Body를 Client에 전송
 */
void send_cache(web_object_t *web_object, int clientfd)
{
  // Response Header 생성 및 전송
  char buf[MAXLINE];
  sprintf(buf, "HTTP/1.0 200 OK\r\n");                                           // 상태 코드
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);                            // 서버 이름
  sprintf(buf, "%sConnection: close\r\n", buf);                                  // 연결 방식
  sprintf(buf, "%sContent-length: %d\r\n\r\n", buf, web_object->content_length); // 컨텐츠 길이
  Rio_writen(clientfd, buf, strlen(buf));

  // 캐싱된 Response Body 전송
  Rio_writen(clientfd, web_object->response_ptr, web_object->content_length);
}

/*
 * read_cache - 사용한 `web_object`를 캐시 연결리스트의 root로 갱신
 */
void read_cache(web_object_t *web_object)
{
  if (web_object == rootp) // 현재 노드가 이미 root면 변경 없이 종료
    return;
  if (web_object->next)
  { // 현재 노드를 연결 리스트에서 제거
    web_object_t *prev_object = web_object->prev;
    web_object_t *next_object = web_object->next;
    if (prev_object)
      web_object->prev->next = next_object;
    web_object->next->prev = prev_object;
  }
  else
  { // 현재 노드가 마지막 노드라면
    web_object->prev->next = NULL;
  }

  // 현재 노드를 root로 변경
  web_object->next = rootp; // root였던 노드는 현재 노드의 다음 노드가 됨
  rootp = web_object;
}

/*
 * write_cache - 캐시 연결리스트에 `web_object` 추가
 */
void write_cache(web_object_t *web_object)
{
  // total_cache_size에 현재 객체의 크기 추가
  total_cache_size += web_object->content_length;

  // 최대 총 캐시 크기를 초과한 경우, 사용한지 가장 오래된 객체부터 제거
  while (total_cache_size > MAX_CACHE_SIZE)
  {
    total_cache_size -= lastp->content_length;
    lastp = lastp->prev; // 마지막 노드를 마지막의 이전 노드로 변경
    free(lastp->next);   // 제거한 노드의 메모리 반환
    lastp->next = NULL;
  }

  if (rootp)
  { // root에 객체 삽입
    web_object->next = rootp;
    rootp->prev = web_object;
  }
  else
  { // 캐시 연결리스트가 빈 경우 lastp를 현재 객체로 지정
    lastp = web_object;
  }
  rootp = web_object;
}

/*
 * print_cache - 캐시 연결리스트 출력
 */
void print_cache()
{
  web_object_t *current = rootp;
  while (current)
  {
    printf("cur: %s / prev: %s / next: %s\n", current->path, current->prev->path, current->next->path);
    current = current->next;
  }
}
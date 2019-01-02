#include <string.h>
#include <math.h>
#include <stdio.h>
#include "simgrid/msg.h"            
#include "rand.h"

int  NUM_CLIENTS;				// número de clientes
int  NUM_SERVERS;				// número de servidores

int  DISPATCHER_STRATEGY;			// Round robin: 1 or a string containing "round" and "robin"
						// Random: anything else, default strategy

#define NUM_DISPATCHERS	1
#define SERVICE_TIME    0.002     		// tiempo medio de servicio = 2 ms
#define INTER_ARRIVAL_TIME    0.010     	// tiempo medio entre peticiones en los clientes = 10 ms

#define NUM_TASKS	10000			// número de tareas a generar en cada cliente

#define SERVICE_RATE    (1/SERVICE_TIME)     	// tasa de servicio, exponencial de media 2 ms
#define ARRIVAL_RATE    (1/INTER_ARRIVAL_TIME) 	// tasa de llegadas entre peticiones


#define FINALIZE ((void*)221297)      		// mensaje de finalización

msg_bar_t barrier_clients;
msg_bar_t barrier_servers;

// estructura de la petcición que envía el cliente 
struct ClientRequest {
	int    n_task;		// numero de tarea
	int    n_client;	// cliente que genera la tarea 
	double t_arrival;   	//  momento en el que swe gerna la tarea 
	double t_service;   	// tiempo de servicio  de la tarea
};

// Client function: genera las peticiones
int cliente(int argc, char *argv[])
{
  	double task_comp_size = 0;
  	double task_comm_size = 2000;  // petición de 2000 bytes
  	char sprintf_buffer[64];
  	char mailbox[256];
  	msg_task_t task = NULL;
  	msg_task_t ans = NULL;
  	struct ClientRequest req, *rq;
  	double t_arrival;
	int my_c;
	int k;
	double timeServiceAvg = 0.0;
	double c1, c2;
	int nd = 0;

	my_c = atoi(argv[0]);
	MSG_mailbox_set_async(MSG_host_get_name(MSG_host_self()));

	for (k=0; k <NUM_TASKS; k++) {

		// espera a crear la petición
		t_arrival = exponential((double)ARRIVAL_RATE);
		MSG_process_sleep(t_arrival);

      		/* crea la tarea */
      		sprintf(sprintf_buffer, "Task_%d", k);
      		req.t_arrival = MSG_get_clock();   // tiempo de llegada

		// tiempo de servicio asignada a la tarea
                // t medio de servicio = 1/SERVICE_RATE de seg
		req.t_service = exponential((double)SERVICE_RATE);

		req.n_task = k;
		req.n_client = my_c;
		task_comp_size = req.t_service;
                task_comm_size = 0;

		c1 = MSG_get_clock();

      		task = MSG_task_create(sprintf_buffer, task_comp_size, task_comm_size,NULL);

		rq = (struct ClientRequest *) malloc(sizeof(struct ClientRequest));
      		*rq = req;
      		MSG_task_set_data(task, (void *) rq );

		// ahora se la envía a un único dispather
		nd = (nd+1) % NUM_DISPATCHERS;;

      		sprintf(mailbox, "d-%d", nd);
      		MSG_task_send(task, mailbox);   


		// espera la respuesta del servidor
		//
                int res = MSG_task_receive(&(ans), MSG_host_get_name(MSG_host_self()));
                if (res != MSG_OK)
                        break;
    		MSG_task_destroy(ans);
		ans = NULL;


		c2 = MSG_get_clock();
		timeServiceAvg = timeServiceAvg + (c2 - c1);
	}

	MSG_barrier_wait(barrier_clients);  // se esperan todos los clientes
	
	/* finalizar */
  	sprintf(mailbox, "d-%d", 0);
    	msg_task_t finalize = MSG_task_create("finalize", 0, 0, FINALIZE);
    	MSG_task_send(finalize, mailbox);

	printf("Cliente%d tiempo medio de servicio = %g ms\n", my_c, timeServiceAvg/NUM_TASKS);

  	return 0;
}                               


// dispatcher function, recibe las peticiones clientes y las envía a los servidores
int dispatcher(int argc, char *argv[])
{
        msg_task_t task = NULL;
        msg_task_t new_task = NULL;
	struct ClientRequest *req, *reqs;
	int my_d;
  	int res;
	char mailbox[64];
	int ns;
	int nt = 0;

	my_d = atoi(argv[0]);
	MSG_mailbox_set_async(MSG_host_get_name(MSG_host_self()));

	while (1) {
                res = MSG_task_receive(&(task), MSG_host_get_name(MSG_host_self()));

                if (res != MSG_OK)
                        break;

		 if (!strcmp(MSG_task_get_name(task), "finalize")) {
                        MSG_task_destroy(task);
                        break;
                }

		////////////////////////////////////////////////////////////
		// ahora viene el algoritmo concreto del dispatcher	
		// determina el servidor donde enviar la petición
		// para su procesamiento cuando haya varios servidores

		// Dependiendo de la estrategia de asignación, se hará 
		// aleatoriamente (0) o mediante round robin
		if (DISPATCHER_STRATEGY==0)
			ns = uniform_int(0, NUM_SERVERS-1);	// Servidor seleccionado	
		else if(DISPATCHER_STRATEGY==1){
			nt++;
			ns = nt%NUM_SERVERS; 			// Servidor seleccionado	
		}
		else
			ns = 0;

		if(ns<0 || ns>=NUM_SERVERS)
			printf("\tError, servidor s-%d fuera de rango\n", ns);

		//////////////////////////////////////////////////////////////
		//

		req = MSG_task_get_data(task);
		reqs = (struct ClientRequest *) malloc(sizeof(struct ClientRequest));
		*reqs = *req;
	 	new_task = MSG_task_create(MSG_task_get_name(task), 
						MSG_task_get_flops_amount(task),
						MSG_task_get_bytes_amount(task), 
						NULL);
			
		MSG_task_set_data(new_task, (void *) reqs );
		free(req);
		MSG_task_destroy(task);

                sprintf(mailbox, "s-%d", ns);
                MSG_task_send(new_task, mailbox);
                task = NULL;
        }

	// Mensaje de finalización a cada servidor
       	for(int i=0; i<NUM_SERVERS; i++){
	       	sprintf(mailbox, "s-%d", i);
	       	msg_task_t finalize = MSG_task_create("finalize", 0, 0, FINALIZE);
	       	MSG_task_send(finalize, mailbox);
       	}
	return 0;
}


int server(int argc, char *argv[])
{
	char mailbox[64];
  	msg_task_t task = NULL;
  	msg_task_t ans_task = NULL;
  	struct ClientRequest *req;
  	int res;
	int my_s;
	double c1, c2;
	double f, ts=0.0;
	int n_tasks = 0;
	
	my_s = atoi(argv[0]);
	MSG_mailbox_set_async(MSG_host_get_name(MSG_host_self()));

	c1= MSG_get_clock();

  	while (1) {
    		res = MSG_task_receive(&(task), MSG_host_get_name(MSG_host_self()));

		if (res != MSG_OK)
			break;

		 if (!strcmp(MSG_task_get_name(task), "finalize")) {
                        MSG_task_destroy(task);
                        break;
                }
		req = MSG_task_get_data(task);

		f= MSG_get_clock();
		MSG_process_sleep(req->t_service);	// duerme el tiempo de ejecución

		ans_task = MSG_task_create("anwser", 0, 2000,NULL);
		sprintf(mailbox, "c-%d",req->n_client);
		MSG_task_send(ans_task, mailbox);

		// tiempo de servicio acumulado
		ts= ts + MSG_get_clock() - f;

		// registra el tiempo de servicio de esta tarea desde que se creo

		free(req);
    		MSG_task_destroy(task);
    		task = NULL;
		n_tasks++;
	}  

	c2=MSG_get_clock();

	// Se esperan todos los servidores para que devuelvan sus resultados
	MSG_barrier_wait(barrier_servers);  

	if (ts > (c2-c1)){
		printf("Servidor%d ; tareas: %d ;  Carga: %g%  ; Peticiones/s: %g\n", 
					my_s, n_tasks, 100.0, n_tasks/(c2-c1));
	}
	else{
		printf("Servidor%d ; tareas: %d ; Carga: %g% ;  Peticiones/s: %g \n", 
					my_s, n_tasks, (ts/(c2-c1))*100,  n_tasks/(c2-c1));
	}

  	return 0;
}


void test_all(char *file)
{
	int argc;
        char str[50];
        int i;
        msg_process_t p;

  	MSG_create_environment(file);

	// el proceso cliente es el que genera las peticiones
  	MSG_function_register("cliente", cliente);

	// el proceso dispatcher es el que distribuye las peticiones que le llegan a los servidores
  	MSG_function_register("dispatcher", dispatcher);

	// el proceso servidor sirve las peticiones
  	MSG_function_register("server", server);

	// crea los procesos servidores
	for (i=0; i < NUM_SERVERS; i++) {
                sprintf(str,"s-%d", i);
                argc = 1;
                char **argvc=xbt_new(char*,2);

                argvc[0] = bprintf("%d",i);
                argvc[1] = NULL;

                p = MSG_process_create_with_arguments(str, server, NULL, MSG_get_host_by_name(str), argc, argvc);
                if (p == NULL) {
                        printf("Error en ......... %d\n", i);
                        exit(0);
                }

        }

	// crea los procesos clientes 
	for (i=0; i < NUM_CLIENTS; i++) {
                sprintf(str,"c-%d", i);
                argc = 1;
                char **argvc=xbt_new(char*,2);

                argvc[0] = bprintf("%d",i);
                argvc[1] = NULL;

                p = MSG_process_create_with_arguments(str, cliente, NULL, MSG_get_host_by_name(str), argc, argvc);
                if (p == NULL) {
                        printf("Error en ......... %d\n", i);
                        exit(0);
                }

        }

	// crea los procesos dispatchers
	for (i=0; i < NUM_DISPATCHERS; i++) {
                sprintf(str,"d-%d", i);
                argc = 1;
                char **argvc=xbt_new(char*,2);

                argvc[0] = bprintf("%d",i);
                argvc[1] = NULL;

                p = MSG_process_create_with_arguments(str, dispatcher, NULL, MSG_get_host_by_name(str), argc, argvc);
                if (p == NULL) {
                        printf("Error en ......... %d\n", i);
                        exit(0);
		}
	}

	return;
}



/** Main function */
int main(int argc, char *argv[])
{
  	msg_error_t res = MSG_OK;
	int i;

	 if (argc < 3) {
                printf("Usage: %s platform_file num_clients \n", argv[0]);
                exit(1);
        }

        seed((int) time(NULL));

	// Recogida del número de clientes del segundo argumento
	NUM_CLIENTS = atoi(argv[2]);

	// Recogida del número de servidores del tercer argumento (1 en caso de no haberlo)
	if (argc > 3){
		NUM_SERVERS = atoi(argv[3]);
	}
	else
		NUM_SERVERS = 1;

	// Recogida del tipo de método de asignación de trabajos a los srevidores
	// Round robin: 1 o una cadena que contenga "round" y "robin"
	// Random: cualquier otra cosa, estrategia por defecto
	if (argc > 4 && ((strstr(argv[4], "round")!=NULL && strstr(argv[4], "robin")!=NULL) || atoi(argv[4])==1))
		DISPATCHER_STRATEGY = 1;
	else
		DISPATCHER_STRATEGY = 0;

  	MSG_init(&argc, argv);

	// Se crean las barreras tanto para los clientes como para los servidores
	barrier_clients=MSG_barrier_init(NUM_CLIENTS);
	barrier_servers=MSG_barrier_init(NUM_SERVERS);

	test_all(argv[1]);

  	res = MSG_main();


	//printf("Simulation time %g\n", MSG_get_clock());

  	if (res == MSG_OK)
    		return 0;
  	else
    		return 1;
}
